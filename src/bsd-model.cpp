/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#include "bsd-model.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <set>

namespace bsd {

/* Pure tree lookup/editing lives in bsd-tree.cpp (no OBS dependency). This file
 * holds the OBS-aware parts: reconciling against the real scene list and JSON
 * persistence. */

/* ---- reconcile with the real OBS scene list ----------------------------- */

static void collect_scene_names(const Node &n, std::set<std::string> &out)
{
	if (n.type == NodeType::Scene)
		out.insert(n.name);
	for (auto &c : n.children)
		collect_scene_names(*c, out);
}

static bool prune_missing(Node &n, const std::set<std::string> &existing)
{
	bool changed = false;
	auto &ch = n.children;
	for (auto it = ch.begin(); it != ch.end();) {
		Node *c = it->get();
		if (c->type == NodeType::Scene && !existing.count(c->name)) {
			it = ch.erase(it);
			changed = true;
		} else {
			if (c->is_container())
				changed |= prune_missing(*c, existing);
			++it;
		}
	}
	return changed;
}

bool Model::reconcile_with_obs()
{
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	std::set<std::string> existing;
	for (size_t i = 0; i < scenes.sources.num; i++)
		existing.insert(obs_source_get_name(scenes.sources.array[i]));

	bool changed = prune_missing(root, existing);

	std::set<std::string> inTree;
	collect_scene_names(root, inTree);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const char *name = obs_source_get_name(scenes.sources.array[i]);
		if (inTree.count(name))
			continue;
		auto node = std::make_unique<Node>();
		node->type = NodeType::Scene;
		node->id = name;
		node->name = name;
		root.children.push_back(std::move(node));
		changed = true;
	}

	obs_frontend_source_list_free(&scenes);
	return changed;
}

/* ---- persistence (own JSON file in the module config dir) --------------- */

static const char *type_str(NodeType t)
{
	switch (t) {
	case NodeType::Folder:
		return "folder";
	case NodeType::Divider:
		return "divider";
	case NodeType::Scene:
		return "scene";
	default:
		return "root";
	}
}

obs_data_t *Model::serialize_node(const Node &n) const
{
	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "type", type_str(n.type));
	obs_data_set_string(d, "id", n.id.c_str());
	obs_data_set_string(d, "name", n.name.c_str());
	obs_data_set_string(d, "color", n.color.c_str());
	obs_data_set_bool(d, "collapsed", n.collapsed);
	if (n.is_container() && !n.children.empty()) {
		obs_data_array_t *arr = obs_data_array_create();
		for (auto &c : n.children) {
			obs_data_t *cd = serialize_node(*c);
			obs_data_array_push_back(arr, cd);
			obs_data_release(cd);
		}
		obs_data_set_array(d, "children", arr);
		obs_data_array_release(arr);
	}
	return d;
}

void Model::deserialize_into(Node &parent, obs_data_array_t *arr)
{
	const size_t count = obs_data_array_count(arr);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *d = obs_data_array_item(arr, i);
		const std::string ts = obs_data_get_string(d, "type");
		auto node = std::make_unique<Node>();
		node->type = ts == "folder" ? NodeType::Folder : ts == "divider" ? NodeType::Divider : NodeType::Scene;
		node->id = obs_data_get_string(d, "id");
		node->name = obs_data_get_string(d, "name");
		node->color = obs_data_get_string(d, "color");
		node->collapsed = obs_data_get_bool(d, "collapsed");
		if (node->is_container()) {
			obs_data_array_t *kids = obs_data_get_array(d, "children");
			if (kids) {
				deserialize_into(*node, kids);
				obs_data_array_release(kids);
			}
		}
		parent.children.push_back(std::move(node));
		obs_data_release(d);
	}
}

std::string Model::config_file_path()
{
	char *path = obs_module_config_path("structure.json");
	std::string ret = path ? path : "";
	bfree(path);
	return ret;
}

void Model::load_for_collection(const std::string &collection)
{
	root.children.clear();

	const std::string path = config_file_path();
	if (path.empty())
		return;
	obs_data_t *all = obs_data_create_from_json_file(path.c_str());
	if (!all)
		return;
	obs_data_t *coll = obs_data_get_obj(all, collection.c_str());
	if (coll) {
		obs_data_array_t *arr = obs_data_get_array(coll, "children");
		if (arr) {
			deserialize_into(root, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(coll);
	}
	obs_data_release(all);
}

void Model::save_for_collection(const std::string &collection) const
{
	const std::string path = config_file_path();
	if (path.empty())
		return;

	/* Ensure the module config dir exists (obs_module_config_path points
	 * inside it but does not create it). */
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	/* Merge into the existing file so other collections are preserved. */
	obs_data_t *all = obs_data_create_from_json_file(path.c_str());
	if (!all)
		all = obs_data_create();

	obs_data_t *coll = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (auto &c : root.children) {
		obs_data_t *cd = serialize_node(*c);
		obs_data_array_push_back(arr, cd);
		obs_data_release(cd);
	}
	obs_data_set_array(coll, "children", arr);
	obs_data_array_release(arr);
	obs_data_set_obj(all, collection.c_str(), coll);
	obs_data_release(coll);

	if (!obs_data_save_json(all, path.c_str()))
		obs_log(LOG_WARNING, "could not write structure to %s", path.c_str());
	obs_data_release(all);
}

} // namespace bsd
