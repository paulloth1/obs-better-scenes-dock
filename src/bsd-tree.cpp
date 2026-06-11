/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

/*
 * bsd-tree.cpp — the pure tree logic of the model: lookup and structural edits.
 *
 * Deliberately free of any OBS dependency so it can be unit-tested standalone
 * (see tests/). The OBS-aware parts (reconcile, JSON persistence) live in
 * bsd-model.cpp.
 */

#include "bsd-model.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace bsd {

static std::string gen_id(const char *prefix)
{
	/* Unique-enough id for folders/dividers: prefix + monotonic time + a
	 * per-process counter (guards against two ids in the same nanosecond). */
	static std::atomic<uint64_t> counter{0};
	const uint64_t t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
	const uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
	char buf[80];
	snprintf(buf, sizeof(buf), "%s_%llx_%llx", prefix, (unsigned long long)t, (unsigned long long)n);
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

} // namespace bsd
