/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <obs.h>

#include <memory>
#include <string>
#include <vector>

namespace bsd {

enum class NodeType { Root, Folder, Divider, Scene };

/* The dock shows a tree the plugin owns. Folder/Divider nodes are pure plugin
 * metadata; Scene nodes reference a real OBS scene by name (name == scene
 * name). The structure is persisted per scene collection in our own JSON file
 * under the module config dir — independent of OBS' scene_order, which keeps
 * us robust against OBS' list internals. */
struct Node {
	NodeType type = NodeType::Folder;
	std::string id;    // stable id for folders/dividers; for scenes == scene name
	std::string name;  // folder/divider label, or the scene name
	std::string color; // "#RRGGBB" or "" (theme default)
	bool collapsed = false;
	std::vector<std::unique_ptr<Node>> children; // root/folders only

	bool is_container() const { return type == NodeType::Root || type == NodeType::Folder; }
};

class Model {
public:
	Node root; // type == Root

	Model() { root.type = NodeType::Root; }

	/* Drops scene nodes whose scene no longer exists and appends any OBS
	 * scene not yet in the tree (in OBS order) at the end of the root.
	 * Returns true if the tree changed. */
	bool reconcile_with_obs();

	/* Find helpers (search the whole tree). */
	Node *find(const std::string &id);
	Node *find_parent(const Node *child);

	/* Structural edits. Return the new/affected node (or nullptr). */
	Node *add_folder(Node *parent, const std::string &name);
	Node *add_divider(Node *parent, const std::string &name);
	bool remove_node(Node *node);                 // folders/dividers; scene children move to parent
	bool move_node(Node *node, Node *newParent, int index);
	bool move_within_parent(Node *node, int delta); // -1 = up, +1 = down

	/* JSON persistence keyed by scene collection name (own config file). */
	void load_for_collection(const std::string &collection);
	void save_for_collection(const std::string &collection) const;

private:
	obs_data_t *serialize_node(const Node &n) const;
	void deserialize_into(Node &parent, obs_data_array_t *arr);
	static std::string config_file_path();
};

} // namespace bsd
