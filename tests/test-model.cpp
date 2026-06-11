/*
Better Scenes Dock — unit tests for the pure tree logic (bsd-tree.cpp).
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

These tests deliberately avoid the OBS SDK: they cover only the OBS-free tree
operations (lookup + structural edits), so they compile and run anywhere with
just a C++ compiler. The OBS-aware persistence/reconcile code is not tested here.
*/

#include "bsd-model.hpp"

#include <cstdio>
#include <string>

using namespace bsd;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
	do {                                                              \
		++g_checks;                                               \
		if (!(cond)) {                                            \
			++g_failures;                                    \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		}                                                        \
	} while (0)

/* Helper: append a scene node (scenes normally arrive via reconcile_with_obs). */
static Node *add_scene(Node *parent, const std::string &name)
{
	auto node = std::make_unique<Node>();
	node->type = NodeType::Scene;
	node->id = name;
	node->name = name;
	Node *ret = node.get();
	parent->children.push_back(std::move(node));
	return ret;
}

static void test_add_and_find()
{
	Model m;
	Node *f = m.add_folder(&m.root, "Games");
	CHECK(f != nullptr);
	CHECK(f->type == NodeType::Folder);
	CHECK(!f->id.empty());
	CHECK(m.find(f->id) == f);
	CHECK(m.find_parent(f) == &m.root);

	Node *d = m.add_divider(&m.root, "Break");
	CHECK(d->type == NodeType::Divider);
	CHECK(d->id != f->id); // ids are unique

	Node *s = add_scene(f, "Intro");
	CHECK(m.find("Intro") == s);
	CHECK(m.find_parent(s) == f);
	CHECK(m.find("does-not-exist") == nullptr);
}

static void test_add_folder_defaults_to_root()
{
	Model m;
	/* A null/non-container parent falls back to the root. */
	Node *f = m.add_folder(nullptr, "X");
	CHECK(m.find_parent(f) == &m.root);
	Node *s = add_scene(&m.root, "Cam");
	Node *bad = m.add_folder(s, "Y"); // scene isn't a container
	CHECK(m.find_parent(bad) == &m.root);
}

static void test_move_node()
{
	Model m;
	Node *a = m.add_folder(&m.root, "A");
	Node *b = m.add_folder(&m.root, "B");
	Node *s = add_scene(&m.root, "Scene");

	CHECK(m.move_node(s, a, 0));
	CHECK(m.find_parent(s) == a);
	CHECK(a->children.size() == 1);

	/* Move into b at index 0. */
	CHECK(m.move_node(s, b, 0));
	CHECK(m.find_parent(s) == b);
	CHECK(a->children.empty());

	/* A folder may not be moved inside its own subtree. */
	Node *inner = m.add_folder(a, "inner");
	CHECK(m.move_node(a, inner, 0) == false);
	CHECK(m.find_parent(a) == &m.root);
}

static void test_move_within_parent()
{
	Model m;
	Node *s1 = add_scene(&m.root, "one");
	Node *s2 = add_scene(&m.root, "two");
	Node *s3 = add_scene(&m.root, "three");

	CHECK(m.move_within_parent(s3, -1)); // three moves up: one, three, two
	CHECK(m.root.children[1].get() == s3);
	CHECK(m.root.children[2].get() == s2);

	CHECK(m.move_within_parent(s1, +1)); // one moves down: three, one, two
	CHECK(m.root.children[0].get() == s3);
	CHECK(m.root.children[1].get() == s1);

	CHECK(m.move_within_parent(m.root.children[0].get(), -1) == false); // already top
	CHECK(m.move_within_parent(m.root.children[2].get(), +1) == false); // already bottom
}

static void test_remove_rescues_children()
{
	Model m;
	Node *a = m.add_folder(&m.root, "A");
	Node *f = m.add_folder(&m.root, "F"); // index 1
	Node *b = m.add_folder(&m.root, "B"); // index 2
	(void)a;
	(void)b;
	Node *s1 = add_scene(f, "s1");
	Node *s2 = add_scene(f, "s2");

	const std::string fid = f->id; // f is destroyed by remove_node below
	CHECK(m.remove_node(f));
	CHECK(m.find(fid) == nullptr);
	/* s1, s2 lifted into the root at F's old position (index 1, 2). */
	CHECK(m.find_parent(s1) == &m.root);
	CHECK(m.find_parent(s2) == &m.root);
	CHECK(m.root.children[1].get() == s1);
	CHECK(m.root.children[2].get() == s2);

	/* The root itself cannot be removed. */
	CHECK(m.remove_node(&m.root) == false);
}

int main()
{
	test_add_and_find();
	test_add_folder_defaults_to_root();
	test_move_node();
	test_move_within_parent();
	test_remove_rescues_children();

	std::printf("bsd model tests: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
