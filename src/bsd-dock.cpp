/*
Better Scenes Dock — folders, dividers & colors for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

/*
 * bsd-dock.cpp — the "Better Scenes" dock.
 *
 * A QTreeWidget renders a plugin-owned tree (folders, dividers, scene refs;
 * see bsd-model). Clicking a scene switches it (preview scene in studio mode,
 * program otherwise). Folders are collapsible; dividers are styled lines.
 * Structure, colors and collapse state are persisted per scene collection.
 * On load the native OBS "scenesDock" is hidden so this dock replaces it.
 */

#include "bsd-dock.hpp"
#include "bsd-model.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QColor>
#include <QColorDialog>
#include <QDockWidget>
#include <QInputDialog>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <string>

namespace bsd {

enum {
	NodeIdRole = Qt::UserRole,
	NodeTypeRole,
	NodeColorRole,
	NodeStateRole, // 0 none, 1 program, 2 preview
};

static std::string current_collection()
{
	char *c = obs_frontend_get_current_scene_collection();
	std::string s = c ? c : "";
	bfree(c);
	return s;
}

/* ----------------------------------------------------------------------- */

class ItemDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		const int type = index.data(NodeTypeRole).toInt();
		const QColor accent(index.data(NodeColorRole).toString());
		const int state = index.data(NodeStateRole).toInt();

		if (type == (int)NodeType::Divider) {
			const QString label = index.data(Qt::DisplayRole).toString().trimmed();
			QColor c = accent.isValid() ? accent : option.palette.color(QPalette::Disabled, QPalette::Text);
			p->save();
			p->setRenderHint(QPainter::Antialiasing, true);
			QColor bg = c;
			bg.setAlpha(28);
			p->fillRect(option.rect, bg);
			p->setPen(QPen(c, 1));
			const QRect r = option.rect.adjusted(6, 0, -6, 0);
			const int y = r.center().y();
			if (label.isEmpty()) {
				p->drawLine(r.left(), y, r.right(), y);
			} else {
				QFont f = option.font;
				f.setBold(true);
				f.setCapitalization(QFont::AllUppercase);
				p->setFont(f);
				const QFontMetrics fm(f);
				const QString el = fm.elidedText(label, Qt::ElideRight, r.width() - 20);
				const int tw = fm.horizontalAdvance(el);
				const int tx = r.center().x() - tw / 2;
				if (tx - 8 > r.left())
					p->drawLine(r.left(), y, tx - 8, y);
				if (tx + tw + 8 < r.right())
					p->drawLine(tx + tw + 8, y, r.right(), y);
				p->drawText(QRect(tx, option.rect.top(), tw, option.rect.height()), Qt::AlignCenter, el);
			}
			p->restore();
			return;
		}

		QStyleOptionViewItem opt = option;
		if (state != 0)
			opt.font.setBold(true);
		if (accent.isValid() && state == 0) {
			opt.palette.setColor(QPalette::Text, accent);
			opt.palette.setColor(QPalette::HighlightedText, accent);
		}
		QStyledItemDelegate::paint(p, opt, index);

		/* Left accent bar: program red, preview green, else node color. */
		QColor bar;
		if (state == 1)
			bar = QColor(0xC8, 0x32, 0x2D);
		else if (state == 2)
			bar = QColor(0x2E, 0x9E, 0x4F);
		else if (accent.isValid())
			bar = accent;
		if (bar.isValid()) {
			p->save();
			p->fillRect(QRect(option.rect.left(), option.rect.top(), 3, option.rect.height()), bar);
			p->restore();
		}
	}
};

/* ----------------------------------------------------------------------- */

class BetterScenesDock : public QWidget {
public:
	explicit BetterScenesDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		tree_ = new QTreeWidget(this);
		tree_->setHeaderHidden(true);
		tree_->setRootIsDecorated(true);
		tree_->setIndentation(14);
		tree_->setUniformRowHeights(true);
		tree_->setExpandsOnDoubleClick(true);
		tree_->setSelectionMode(QAbstractItemView::SingleSelection);
		tree_->setContextMenuPolicy(Qt::CustomContextMenu);
		tree_->setItemDelegate(new ItemDelegate(tree_));

		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(tree_);

		connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) { activate(it); });
		connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) { setCollapsed(it, false); });
		connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *it) { setCollapsed(it, true); });
		connect(tree_, &QTreeWidget::customContextMenuRequested, this,
			[this](const QPoint &pos) { showMenu(pos); });

		collection_ = current_collection();
		model_.load_for_collection(collection_);
		model_.reconcile_with_obs();
		rebuild();
		hideNativeDock();
	}

	void onFrontendEvent(enum obs_frontend_event event)
	{
		switch (event) {
		case OBS_FRONTEND_EVENT_FINISHED_LOADING:
			hideNativeDock();
			model_.reconcile_with_obs();
			rebuild();
			break;
		case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
			if (model_.reconcile_with_obs()) {
				save();
				rebuild();
			} else {
				updateHighlight();
			}
			break;
		case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
			updateHighlight();
			break;
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
			save();
			break;
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
			collection_ = current_collection();
			model_.load_for_collection(collection_);
			model_.reconcile_with_obs();
			rebuild();
			break;
		case OBS_FRONTEND_EVENT_EXIT:
			save();
			break;
		default:
			break;
		}
	}

private:
	QTreeWidget *tree_;
	Model model_;
	std::string collection_;
	bool updating_ = false;

	void save() { model_.save_for_collection(collection_); }

	Node *nodeOf(QTreeWidgetItem *item)
	{
		if (!item)
			return nullptr;
		return model_.find(item->data(0, NodeIdRole).toString().toStdString());
	}

	/* ---- build ---- */

	void rebuild()
	{
		updating_ = true;
		tree_->clear();
		for (auto &child : model_.root.children)
			addNode(*child, nullptr);
		updating_ = false;
		updateHighlight();
	}

	void addNode(Node &n, QTreeWidgetItem *parentItem)
	{
		auto *item = new QTreeWidgetItem();
		item->setData(0, NodeIdRole, QString::fromStdString(n.id));
		item->setData(0, NodeTypeRole, (int)n.type);
		item->setData(0, NodeColorRole, QString::fromStdString(n.color));
		item->setText(0, QString::fromStdString(n.name));

		Qt::ItemFlags flags = Qt::ItemIsEnabled;
		if (n.type != NodeType::Divider)
			flags |= Qt::ItemIsSelectable;
		item->setFlags(flags);

		if (parentItem)
			parentItem->addChild(item);
		else
			tree_->addTopLevelItem(item);

		if (n.is_container()) {
			for (auto &c : n.children)
				addNode(*c, item);
			item->setExpanded(!n.collapsed);
		}
	}

	/* ---- interaction ---- */

	void activate(QTreeWidgetItem *item)
	{
		if (updating_)
			return;
		Node *n = nodeOf(item);
		if (!n || n->type != NodeType::Scene)
			return;
		obs_source_t *scene = obs_get_source_by_name(n->name.c_str());
		if (scene) {
			if (obs_frontend_preview_program_mode_active())
				obs_frontend_set_current_preview_scene(scene);
			else
				obs_frontend_set_current_scene(scene);
			obs_source_release(scene);
		}
	}

	void setCollapsed(QTreeWidgetItem *item, bool collapsed)
	{
		if (updating_)
			return;
		if (Node *n = nodeOf(item)) {
			if (n->is_container() && n->collapsed != collapsed) {
				n->collapsed = collapsed;
				save();
			}
		}
	}

	/* ---- highlight current program / preview scene ---- */

	void updateHighlight()
	{
		std::string program, preview;
		if (obs_source_t *s = obs_frontend_get_current_scene()) {
			program = obs_source_get_name(s);
			obs_source_release(s);
		}
		const bool studio = obs_frontend_preview_program_mode_active();
		if (studio) {
			if (obs_source_t *s = obs_frontend_get_current_preview_scene()) {
				preview = obs_source_get_name(s);
				obs_source_release(s);
			}
		}

		QTreeWidgetItemIterator it(tree_);
		QTreeWidgetItem *active = nullptr;
		for (; *it; ++it) {
			QTreeWidgetItem *item = *it;
			if (item->data(0, NodeTypeRole).toInt() != (int)NodeType::Scene)
				continue;
			const std::string name = item->text(0).toStdString();
			int state = 0;
			if (name == program)
				state = 1;
			else if (studio && name == preview)
				state = 2;
			item->setData(0, NodeStateRole, state);
			if ((studio && state == 2) || (!studio && state == 1))
				active = item;
		}
		if (active) {
			updating_ = true;
			tree_->setCurrentItem(active);
			updating_ = false;
		}
		tree_->viewport()->update();
	}

	/* ---- context menu ---- */

	void showMenu(const QPoint &pos)
	{
		QTreeWidgetItem *item = tree_->itemAt(pos);
		Node *n = nodeOf(item);
		QMenu menu;

		menu.addAction(obs_module_text("BSD.NewFolder"), this, [this, n]() { newFolder(n); });
		menu.addAction(obs_module_text("BSD.NewDivider"), this, [this, n]() { newDivider(n); });

		if (n) {
			menu.addSeparator();
			if (n->type == NodeType::Folder || n->type == NodeType::Divider)
				menu.addAction(obs_module_text("BSD.Rename"), this, [this, n]() { renameNode(n); });
			menu.addAction(obs_module_text("BSD.SetColor"), this, [this, n]() { setColor(n); });
			if (!n->color.empty())
				menu.addAction(obs_module_text("BSD.ClearColor"), this, [this, n]() {
					n->color.clear();
					save();
					rebuild();
				});

			QMenu *moveMenu = menu.addMenu(obs_module_text("BSD.MoveTo"));
			addMoveTargets(moveMenu, &model_.root, n, 0);

			if (n->type == NodeType::Folder || n->type == NodeType::Divider) {
				menu.addSeparator();
				menu.addAction(obs_module_text("BSD.Remove"), this, [this, n]() {
					model_.remove_node(n);
					save();
					rebuild();
				});
			}
		}
		menu.exec(tree_->viewport()->mapToGlobal(pos));
	}

	void addMoveTargets(QMenu *menu, Node *container, Node *moving, int depth)
	{
		/* Skip the moving node's own subtree as a destination. */
		if (container == moving)
			return;
		const QString label = container->type == NodeType::Root
					      ? QString(obs_module_text("BSD.Root"))
					      : QString(depth, QChar(0x2003)) + QString::fromStdString(container->name);
		menu->addAction(label, this, [this, container, moving]() {
			model_.move_node(moving, container, (int)container->children.size());
			save();
			rebuild();
		});
		for (auto &c : container->children) {
			if (c->type == NodeType::Folder)
				addMoveTargets(menu, c.get(), moving, depth + 1);
		}
	}

	Node *containerFor(Node *n)
	{
		if (!n)
			return &model_.root;
		if (n->is_container())
			return n;
		Node *p = model_.find_parent(n);
		return p ? p : &model_.root;
	}

	void newFolder(Node *context)
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, obs_module_text("BSD.NewFolder"),
							   obs_module_text("BSD.FolderNamePrompt"), QLineEdit::Normal,
							   obs_module_text("BSD.FolderDefault"), &ok);
		if (!ok)
			return;
		model_.add_folder(containerFor(context), name.trimmed().toStdString());
		save();
		rebuild();
	}

	void newDivider(Node *context)
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, obs_module_text("BSD.NewDivider"),
							   obs_module_text("BSD.DividerNamePrompt"), QLineEdit::Normal,
							   QString(), &ok);
		if (!ok)
			return;
		model_.add_divider(containerFor(context), name.trimmed().toStdString());
		save();
		rebuild();
	}

	void renameNode(Node *n)
	{
		bool ok = false;
		const QString name = QInputDialog::getText(this, obs_module_text("BSD.Rename"),
							   obs_module_text("BSD.RenamePrompt"), QLineEdit::Normal,
							   QString::fromStdString(n->name), &ok);
		if (!ok)
			return;
		n->name = name.trimmed().toStdString();
		save();
		rebuild();
	}

	void setColor(Node *n)
	{
		QColor initial(QString::fromStdString(n->color));
		const QColor c = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::gray), this,
							obs_module_text("BSD.SetColor"));
		if (!c.isValid())
			return;
		n->color = c.name().toStdString();
		save();
		rebuild();
	}

	/* ---- native dock ---- */

	void hideNativeDock()
	{
		auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!main)
			return;
		if (QDockWidget *d = main->findChild<QDockWidget *>("scenesDock"))
			d->hide();
	}
};

/* ----------------------------------------------------------------------- */

static QPointer<BetterScenesDock> s_dock;

static void frontend_event(enum obs_frontend_event event, void *)
{
	if (s_dock)
		s_dock->onFrontendEvent(event);
}

void dock_register()
{
	s_dock = new BetterScenesDock();
	if (!obs_frontend_add_dock_by_id("better-scenes-dock", obs_module_text("BSD.DockTitle"), s_dock)) {
		obs_log(LOG_WARNING, "could not register Better Scenes dock");
		delete s_dock;
		s_dock = nullptr;
		return;
	}
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_log(LOG_INFO, "Better Scenes dock registered");
}

void dock_unregister()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	obs_frontend_remove_dock("better-scenes-dock");
	s_dock = nullptr;
}

} // namespace bsd
