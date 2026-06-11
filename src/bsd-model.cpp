/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#include "bsd-model.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <set>

namespace bsd {

static std::string gen_id(const char *prefix)
{
	/* Unique-enough id for folders/dividers: prefix + high-res time. */
	char buf[64];
	snprintf(buf, sizeof(buf), "%s_%llx", prefix, (unsigned long long)os_gettime_ns());
	return buf;
}

/* ---- tree walking ------------------------------------------------------- */

static Node *find_in(Node &n, const std::string &id)
{
	if (n.id == id && n.type != NodeType::Root)
		return &n;
	for (auto &c : n.children) {
		if (Node *hit = find_in(*c, id))
			return hit;
	}
	return nullptr;
}

Node *Model::find(const std::string &id)
{
	return find_in(root, id);
}

static Node *find_parent_in(Node &n, const Node *child)
{
	for (auto &c : n.children) {
		if (c.get() == child)
			return &n;
		if (Node *hit = find_parent_in(*c, child))
			return hit;
	}
	return nullptr;
}

Node *Model::find_parent(const Node *child)
{
	return find_parent_in(root, child);
}

/* ---- structural edits --------------------------------------------------- */

Node *Model::add_folder(Node *parent, const std::string &name)
{
	if (!parent || !parent->is_container())
		parent = &root;
	auto node = std::make_unique<Node>();
	node->type = NodeType::Folder;
	node->id = gen_id("f");
	node->name = name.empty() ? "Folder" : name;
	Node *ret = node.get();
	parent->children.push_back(std::move(node));
	return ret;
}

Node *Model::add_divider(Node *parent, const std::string &name)
{
	if (!parent || !parent->is_container())
		parent = &root;
	auto node = std::make_unique<Node>();
	node->type = NodeType::Divider;
	node->id = gen_id("d");
	node->name = name;
	Node *ret = node.get();
	parent->children.push_back(std::move(node));
	return ret;
}

bool Model::remove_node(Node *node)
{
	if (!node || node->type == NodeType::Root)
		return false;
	Node *parent = find_parent(node);
	if (!parent)
		return false;

	/* When removing a folder, lift its scene/sub nodes into the parent so
	 * no real scene silently disappears from the dock. */
	std::vector<std::unique_ptr<Node>> rescued;
	for (auto &c : node->children)
		rescued.push_back(std::move(c));

	auto &siblings = parent->children;
	for (auto it = siblings.begin(); it != siblings.end(); ++it) {
		if (it->get() == node) {
			it = siblings.erase(it);
			for (auto &r : rescued)
				it = siblings.insert(it, std::move(r)) + 1;
			return true;
		}
	}
	return false;
}

bool Model::move_node(Node *node, Node *newParent, int index)
{
	if (!node || node->type == NodeType::Root)
		return false;
	if (!newParent || !newParent->is_container())
		newParent = &root;
	/* Disallow moving a folder into its own subtree. */
	for (Node *p = newParent; p; p = find_parent(p)) {
		if (p == node)
			return false;
	}
	Node *oldParent = find_parent(node);
	if (!oldParent)
		return false;

	std::unique_ptr<Node> owned;
	auto &src = oldParent->children;
	for (auto it = src.begin(); it != src.end(); ++it) {
		if (it->get() == node) {
			owned = std::move(*it);
			src.erase(it);
			break;
		}
	}
	if (!owned)
		return false;

	auto &dst = newParent->children;
	if (index < 0 || index > (int)dst.size())
		index = (int)dst.size();
	dst.insert(dst.begin() + index, std::move(owned));
	return true;
}

bool Model::move_within_parent(Node *node, int delta)
{
	if (!node || node->type == NodeType::Root || delta == 0)
		return false;
	Node *parent = find_parent(node);
	if (!parent)
		return false;
	auto &ch = parent->children;
	int idx = -1;
	for (int i = 0; i < (int)ch.size(); i++) {
		if (ch[i].get() == node) {
			idx = i;
			break;
		}
	}
	const int target = idx + delta;
	if (idx < 0 || target < 0 || target >= (int)ch.size())
		return false;
	std::swap(ch[idx], ch[target]);
	return true;
}

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
