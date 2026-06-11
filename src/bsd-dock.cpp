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

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QColor>
#include <QColorDialog>
#include <QConicalGradient>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QWidgetAction>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPointer>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace bsd {

enum {
	NodeIdRole = Qt::UserRole,
	NodeTypeRole,
	NodeColorRole,
	NodeStateRole, // 0 none, 1 program, 2 preview
	NodeCountRole, // folders: number of scene descendants
};

/* Plugin-wide preferences (global, not per scene collection). */
struct Settings {
	bool showSearch = true;
	bool duplicateCopies = false; // Duplicate makes full copies instead of refs
};

/* Total scenes anywhere under a node (used for the folder count badge). */
static int scene_count(const Node &n)
{
	int c = (n.type == NodeType::Scene) ? 1 : 0;
	for (auto &ch : n.children)
		c += scene_count(*ch);
	return c;
}

/* Scene names in dock (depth-first) order — the order we push to OBS. */
static void collect_scene_order(const Node &n, std::vector<std::string> &out)
{
	if (n.type == NodeType::Scene)
		out.push_back(n.name);
	for (auto &c : n.children)
		collect_scene_order(*c, out);
}

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
				p->drawText(QRect(tx, option.rect.top(), tw, option.rect.height()), Qt::AlignCenter,
					    el);
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

		/* Folder scene-count badge, right-aligned and muted. */
		if (type == (int)NodeType::Folder) {
			const int count = index.data(NodeCountRole).toInt();
			if (count > 0) {
				p->save();
				p->setPen(option.palette.color(QPalette::Disabled, QPalette::Text));
				p->drawText(option.rect.adjusted(0, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter,
					    QString::number(count));
				p->restore();
			}
		}
	}
};

/* ----------------------------------------------------------------------- */

class BetterScenesDock : public QWidget {
public:
	explicit BetterScenesDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		loadSettings();

		search_ = new QLineEdit(this);
		search_->setClearButtonEnabled(true);
		search_->setPlaceholderText(obs_module_text("BSD.SearchPlaceholder"));
		connect(search_, &QLineEdit::textChanged, this, [this](const QString &t) { filterTree(t); });

		tree_ = new QTreeWidget(this);
		tree_->setHeaderHidden(true);
		tree_->setRootIsDecorated(true);
		tree_->setIndentation(14);
		tree_->setUniformRowHeights(true);
		tree_->setExpandsOnDoubleClick(true);
		tree_->setSelectionMode(QAbstractItemView::SingleSelection);
		tree_->setContextMenuPolicy(Qt::CustomContextMenu);
		tree_->setItemDelegate(new ItemDelegate(tree_));

		auto *toolbar = buildToolbar();

		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(search_);
		layout->addWidget(tree_);
		layout->addLayout(toolbar);

		connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) { activate(it); });
		connect(tree_, &QTreeWidget::itemDoubleClicked, this,
			[this](QTreeWidgetItem *it, int) { transitionTo(it); });
		connect(tree_, &QTreeWidget::itemExpanded, this,
			[this](QTreeWidgetItem *it) { setCollapsed(it, false); });
		connect(tree_, &QTreeWidget::itemCollapsed, this,
			[this](QTreeWidgetItem *it) { setCollapsed(it, true); });
		connect(tree_, &QTreeWidget::customContextMenuRequested, this,
			[this](const QPoint &pos) { showMenu(pos); });
		connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() { updateToolbar(); });

		/* Keep folder placement when a scene is renamed outside the dock. */
		signal_handler_connect(obs_get_signal_handler(), "source_rename", &BetterScenesDock::onSourceRename,
				       this);

		collection_ = current_collection();
		model_.load_for_collection(collection_);
		model_.reconcile_with_obs();
		rebuild();
		updateToolbar();
		applySearchVisibility();
		hideNativeDock();
	}

	~BetterScenesDock() override
	{
		signal_handler_disconnect(obs_get_signal_handler(), "source_rename", &BetterScenesDock::onSourceRename,
					  this);
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
	QLineEdit *search_ = nullptr;
	QToolButton *addBtn_ = nullptr;
	QToolButton *removeBtn_ = nullptr;
	QToolButton *colorBtn_ = nullptr;
	QToolButton *filtersBtn_ = nullptr;
	QToolButton *upBtn_ = nullptr;
	QToolButton *downBtn_ = nullptr;
	QToolButton *settingsBtn_ = nullptr;
	Model model_;
	Settings settings_;
	std::string collection_;
	std::string copiedFiltersSource_; // scene whose filters were last copied
	bool updating_ = false;

	void save() { model_.save_for_collection(collection_); }

	/* ---- settings persistence (own JSON in the module config dir) ---- */

	void loadSettings()
	{
		char *path = obs_module_config_path("settings.json");
		if (!path)
			return;
		obs_data_t *d = obs_data_create_from_json_file(path);
		bfree(path);
		if (!d)
			return;
		obs_data_set_default_bool(d, "show_search", true);
		settings_.showSearch = obs_data_get_bool(d, "show_search");
		settings_.duplicateCopies = obs_data_get_bool(d, "duplicate_copies");
		obs_data_release(d);
	}

	void saveSettings()
	{
		if (char *dir = obs_module_config_path("")) {
			os_mkdirs(dir);
			bfree(dir);
		}
		char *path = obs_module_config_path("settings.json");
		if (!path)
			return;
		obs_data_t *d = obs_data_create();
		obs_data_set_bool(d, "show_search", settings_.showSearch);
		obs_data_set_bool(d, "duplicate_copies", settings_.duplicateCopies);
		obs_data_save_json(d, path);
		obs_data_release(d);
		bfree(path);
	}

	/* ---- search / filter ---- */

	void applySearchVisibility()
	{
		if (!search_)
			return;
		search_->setVisible(settings_.showSearch);
		if (!settings_.showSearch && !search_->text().isEmpty())
			search_->clear(); // textChanged -> filterTree("") unhides everything
	}

	void filterTree(const QString &query)
	{
		const QString q = query.trimmed();
		for (int i = 0; i < tree_->topLevelItemCount(); i++)
			filterItem(tree_->topLevelItem(i), q, false);
	}

	/* Returns true if the item (or a descendant) is visible. `force` keeps a
	 * whole subtree visible when its folder name matched. */
	bool filterItem(QTreeWidgetItem *item, const QString &q, bool force)
	{
		const int type = item->data(0, NodeTypeRole).toInt();
		const bool empty = q.isEmpty();
		bool selfMatch = force || empty;
		if (!selfMatch && (type == (int)NodeType::Scene || type == (int)NodeType::Folder))
			selfMatch = item->text(0).contains(q, Qt::CaseInsensitive);
		const bool childForce = force || (!empty && type == (int)NodeType::Folder && selfMatch);
		bool childMatch = false;
		for (int i = 0; i < item->childCount(); i++)
			childMatch |= filterItem(item->child(i), q, childForce);

		const bool visible = (type == (int)NodeType::Divider) ? (empty || force) : (selfMatch || childMatch);
		item->setHidden(!visible);
		if (!empty && type == (int)NodeType::Folder && (childMatch || childForce))
			item->setExpanded(true);
		return visible;
	}

	/* ---- toolbar ---- */

	QToolButton *makeButton(const QString &tip)
	{
		auto *b = new QToolButton(this);
		b->setToolTip(tip);
		b->setAutoRaise(true);
		b->setFocusPolicy(Qt::NoFocus);
		b->setFixedSize(26, 24);
		return b;
	}

	/* The text glyphs (＋ － ▲ ▼) render via the widget font; the funnel and
	 * colour swatch are hand-drawn QIcons so they sit at the same visual weight
	 * and size instead of a tiny emoji. */
	static constexpr int kIconPx = 14;

	/* A filter funnel, painted in the current theme's text colour. */
	QIcon funnelIcon() const
	{
		const int s = 16;
		const qreal dpr = devicePixelRatioF();
		QPixmap pm(qRound(s * dpr), qRound(s * dpr));
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		QPen pen(palette().color(QPalette::WindowText), 1.4);
		pen.setJoinStyle(Qt::RoundJoin);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		QPolygonF funnel;
		funnel << QPointF(3, 4) << QPointF(13, 4) << QPointF(9.5, 9) << QPointF(9.5, 13) << QPointF(6.5, 13)
		       << QPointF(6.5, 9);
		p.drawPolygon(funnel);
		p.end();
		return QIcon(pm);
	}

	/* A colour swatch: the node's colour if it has one, otherwise a hue wheel so
	 * the button always reads as "set colour". */
	QIcon swatchIcon(const QColor &c) const
	{
		const int s = 16;
		const qreal dpr = devicePixelRatioF();
		QPixmap pm(qRound(s * dpr), qRound(s * dpr));
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r(2.5, 2.5, 11, 11);
		if (c.isValid()) {
			p.setBrush(c);
		} else {
			QConicalGradient g(QPointF(8, 8), 90);
			g.setColorAt(0.00, QColor(0xe8, 0x3a, 0x3a));
			g.setColorAt(0.25, QColor(0xf2, 0xc4, 0x3d));
			g.setColorAt(0.50, QColor(0x3f, 0xc3, 0x6b));
			g.setColorAt(0.75, QColor(0x3a, 0x8f, 0xe8));
			g.setColorAt(1.00, QColor(0xe8, 0x3a, 0x3a));
			p.setBrush(g);
		}
		p.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
		p.drawRoundedRect(r, 3, 3);
		p.end();
		return QIcon(pm);
	}

	QHBoxLayout *buildToolbar()
	{
		addBtn_ = makeButton(obs_module_text("BSD.Add"));
		addBtn_->setText(QStringLiteral("＋"));
		addBtn_->setPopupMode(QToolButton::InstantPopup);
		/* No drop-down arrow next to the glyph. */
		addBtn_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
		auto *addMenu = new QMenu(addBtn_);
		addMenu->addAction(obs_module_text("BSD.NewScene"), this, [this]() { newScene(); });
		addMenu->addAction(obs_module_text("BSD.NewFolder"), this, [this]() { newFolder(selectedNode()); });
		addMenu->addAction(obs_module_text("BSD.NewDivider"), this, [this]() { newDivider(selectedNode()); });
		addBtn_->setMenu(addMenu);

		removeBtn_ = makeButton(obs_module_text("BSD.Remove"));
		removeBtn_->setText(QStringLiteral("－"));

		colorBtn_ = makeButton(obs_module_text("BSD.SetColor"));
		colorBtn_->setIconSize(QSize(kIconPx, kIconPx));
		colorBtn_->setIcon(swatchIcon(QColor()));

		filtersBtn_ = makeButton(obs_module_text("BSD.Filters"));
		filtersBtn_->setIconSize(QSize(kIconPx, kIconPx));
		filtersBtn_->setIcon(funnelIcon());

		upBtn_ = makeButton(obs_module_text("BSD.MoveUp"));
		upBtn_->setText(QStringLiteral("▲"));
		downBtn_ = makeButton(obs_module_text("BSD.MoveDown"));
		downBtn_->setText(QStringLiteral("▼"));

		settingsBtn_ = makeButton(obs_module_text("BSD.Settings"));
		settingsBtn_->setText(QStringLiteral("⋯"));
		settingsBtn_->setPopupMode(QToolButton::InstantPopup);
		settingsBtn_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
		settingsBtn_->setMenu(buildSettingsMenu());

		connect(removeBtn_, &QToolButton::clicked, this, [this]() { removeSelected(); });
		connect(colorBtn_, &QToolButton::clicked, this, [this]() {
			Node *n = selectedNode();
			if (!n)
				return;
			QMenu m(this);
			buildColorMenu(&m, n);
			m.exec(colorBtn_->mapToGlobal(QPoint(0, colorBtn_->height())));
		});
		connect(filtersBtn_, &QToolButton::clicked, this, [this]() { openFilters(); });
		connect(upBtn_, &QToolButton::clicked, this, [this]() { moveSelected(-1); });
		connect(downBtn_, &QToolButton::clicked, this, [this]() { moveSelected(+1); });

		auto *bar = new QHBoxLayout();
		bar->setContentsMargins(4, 2, 4, 2);
		bar->setSpacing(2);
		bar->addWidget(addBtn_);
		bar->addWidget(removeBtn_);
		bar->addWidget(colorBtn_);
		bar->addWidget(filtersBtn_);
		bar->addStretch();
		bar->addWidget(upBtn_);
		bar->addWidget(downBtn_);
		bar->addWidget(settingsBtn_);
		return bar;
	}

	QMenu *buildSettingsMenu()
	{
		auto *m = new QMenu(this);
		auto *aSearch = m->addAction(obs_module_text("BSD.ShowSearch"));
		auto *aDup = m->addAction(obs_module_text("BSD.DuplicateCopies"));
		for (QAction *a : {aSearch, aDup})
			a->setCheckable(true);
		connect(m, &QMenu::aboutToShow, this, [this, aSearch, aDup]() {
			aSearch->setChecked(settings_.showSearch);
			aDup->setChecked(settings_.duplicateCopies);
		});
		connect(aSearch, &QAction::toggled, this, [this](bool on) {
			settings_.showSearch = on;
			saveSettings();
			applySearchVisibility();
		});
		connect(aDup, &QAction::toggled, this, [this](bool on) {
			settings_.duplicateCopies = on;
			saveSettings();
		});
		return m;
	}

	void updateToolbar()
	{
		Node *n = selectedNode();
		const bool isScene = n && n->type == NodeType::Scene;
		removeBtn_->setEnabled(n != nullptr);
		colorBtn_->setEnabled(n != nullptr);
		colorBtn_->setIcon(swatchIcon(n ? QColor(QString::fromStdString(n->color)) : QColor()));
		filtersBtn_->setEnabled(isScene);
		bool canUp = false, canDown = false;
		if (n) {
			Node *parent = model_.find_parent(n);
			if (parent) {
				auto &ch = parent->children;
				for (int i = 0; i < (int)ch.size(); i++) {
					if (ch[i].get() == n) {
						canUp = i > 0;
						canDown = i < (int)ch.size() - 1;
						break;
					}
				}
			}
		}
		upBtn_->setEnabled(canUp);
		downBtn_->setEnabled(canDown);
	}

	Node *selectedNode() { return nodeOf(tree_->currentItem()); }

	void selectNodeById(const std::string &id)
	{
		QTreeWidgetItemIterator it(tree_);
		for (; *it; ++it) {
			if ((*it)->data(0, NodeIdRole).toString().toStdString() == id) {
				tree_->setCurrentItem(*it);
				return;
			}
		}
	}

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
		updateToolbar();
		if (search_ && !search_->text().trimmed().isEmpty())
			filterTree(search_->text());
		syncSceneOrderToObs();
	}

	void addNode(Node &n, QTreeWidgetItem *parentItem)
	{
		auto *item = new QTreeWidgetItem();
		item->setData(0, NodeIdRole, QString::fromStdString(n.id));
		item->setData(0, NodeTypeRole, (int)n.type);
		item->setData(0, NodeColorRole, QString::fromStdString(n.color));
		item->setText(0, QString::fromStdString(n.name));

		/* Dividers are selectable too (so they can be moved / colored /
		 * removed via the toolbar); clicking one just never switches a
		 * scene — activate() ignores non-scene nodes. */
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

		if (parentItem)
			parentItem->addChild(item);
		else
			tree_->addTopLevelItem(item);

		if (n.is_container()) {
			if (n.type == NodeType::Folder)
				item->setData(0, NodeCountRole, scene_count(n));
			for (auto &c : n.children)
				addNode(*c, item);
			item->setExpanded(!n.collapsed);
		}
	}

	QListWidget *nativeSceneList()
	{
		auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		return main ? main->findChild<QListWidget *>("scenes") : nullptr;
	}

	/* Tell OBS to rebuild its multiview projectors (it connects the scene
	 * list's "scenesReordered" signal to UpdateMultiviewProjectors). */
	void refreshMultiview()
	{
		if (QListWidget *list = nativeSceneList())
			QMetaObject::invokeMethod(list, "scenesReordered", Qt::QueuedConnection);
	}

	/* OBS' native per-scene multiview flag (private setting, defaults to on). */
	bool sceneShowsInMultiview(const std::string &name)
	{
		bool show = true;
		if (obs_source_t *s = obs_get_source_by_name(name.c_str())) {
			obs_data_t *d = obs_source_get_private_settings(s);
			obs_data_set_default_bool(d, "show_in_multiview", true);
			show = obs_data_get_bool(d, "show_in_multiview");
			obs_data_release(d);
			obs_source_release(s);
		}
		return show;
	}

	void setShowInMultiview(const std::string &name, bool show)
	{
		if (obs_source_t *s = obs_get_source_by_name(name.c_str())) {
			obs_data_t *d = obs_source_get_private_settings(s);
			obs_data_set_bool(d, "show_in_multiview", show);
			obs_data_release(d);
			obs_source_release(s);
		}
		refreshMultiview();
	}

	/* Mirror the dock's scene order into OBS' (hidden) scene list, which is
	 * what obs_frontend_get_scenes, the saved scene_order and Multiview all
	 * read from. Always on — the dock order is the source of truth. */
	void syncSceneOrderToObs()
	{
		QListWidget *list = nativeSceneList();
		if (!list)
			return;
		std::vector<std::string> order;
		collect_scene_order(model_.root, order);

		bool moved = false;
		{
			const QSignalBlocker block(list);
			int target = 0;
			for (const auto &name : order) {
				for (int i = target; i < list->count(); i++) {
					if (list->item(i)->text().toStdString() == name) {
						if (i != target) {
							list->insertItem(target, list->takeItem(i));
							moved = true;
						}
						target++;
						break;
					}
				}
			}
		}
		if (moved)
			refreshMultiview(); // keep multiview order in step with the dock
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

	/* Studio Mode: a single click set this scene as preview; double-click
	 * transitions it to program (matching the native scene list). Outside
	 * Studio Mode the single click already switched program, so do nothing. */
	void transitionTo(QTreeWidgetItem *item)
	{
		if (updating_ || !obs_frontend_preview_program_mode_active())
			return;
		Node *n = nodeOf(item);
		if (!n || n->type != NodeType::Scene)
			return;
		obs_source_t *scene = obs_get_source_by_name(n->name.c_str());
		if (scene) {
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

		/* Only repaint the program/preview indicators; never hijack the
		 * user's selection or scroll position. */
		QTreeWidgetItemIterator it(tree_);
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
			menu.addAction(obs_module_text("BSD.Rename"), this, [this, n]() { renameNode(n); });

			if (n->type == NodeType::Scene)
				addSceneActions(menu, n);

			QMenu *colorMenu = menu.addMenu(obs_module_text("BSD.SetColor"));
			buildColorMenu(colorMenu, n);

			QMenu *moveMenu = menu.addMenu(obs_module_text("BSD.MoveTo"));
			addMoveTargets(moveMenu, &model_.root, n, 0);

			menu.addSeparator();
			menu.addAction(obs_module_text("BSD.Remove"), this, [this, n]() { removeNode(n); });
		}
		menu.exec(tree_->viewport()->mapToGlobal(pos));
	}

	/* Scene-only entries: duplicate, projector, transition override, screenshot,
	 * filters. Mirrors the native scene context menu using the frontend API. */
	void addSceneActions(QMenu &menu, Node *n)
	{
		const std::string name = n->name;
		menu.addAction(obs_module_text("BSD.Duplicate"), this, [this, n]() { duplicateScene(n); });
		menu.addSeparator();

		QMenu *proj = menu.addMenu(obs_module_text("BSD.SceneProjector"));
		proj->addAction(obs_module_text("BSD.ProjectorWindowed"), this,
				[name]() { obs_frontend_open_projector("Scene", -1, nullptr, name.c_str()); });
		const auto screens = QGuiApplication::screens();
		for (int i = 0; i < screens.size(); i++) {
			const QString label =
				QString(obs_module_text("BSD.ProjectorMonitor")).arg(i + 1).arg(screens[i]->name());
			proj->addAction(label, this, [name, i]() {
				obs_frontend_open_projector("Scene", i, nullptr, name.c_str());
			});
		}

		addTransitionOverrideMenu(menu, name);

		menu.addAction(obs_module_text("BSD.Screenshot"), this, [name]() {
			obs_source_t *s = obs_get_source_by_name(name.c_str());
			if (s) {
				obs_frontend_take_source_screenshot(s);
				obs_source_release(s);
			}
		});

		QAction *mv = menu.addAction(obs_module_text("BSD.ShowInMultiview"), this,
					     [this, name](bool on) { setShowInMultiview(name, on); });
		mv->setCheckable(true);
		mv->setChecked(sceneShowsInMultiview(name));

		menu.addSeparator();
		menu.addAction(obs_module_text("BSD.Filters"), this, [name]() {
			obs_source_t *s = obs_get_source_by_name(name.c_str());
			if (s) {
				obs_frontend_open_source_filters(s);
				obs_source_release(s);
			}
		});
		menu.addAction(obs_module_text("BSD.CopyFilters"), this,
			       [this, name]() { copiedFiltersSource_ = name; });
		QAction *paste = menu.addAction(obs_module_text("BSD.PasteFilters"), this,
						[this, name]() { pasteFilters(name); });
		paste->setEnabled(!copiedFiltersSource_.empty() && copiedFiltersSource_ != name);
		menu.addSeparator();
	}

	void addTransitionOverrideMenu(QMenu &menu, const std::string &sceneName)
	{
		obs_source_t *scene = obs_get_source_by_name(sceneName.c_str());
		if (!scene)
			return;
		obs_data_t *data = obs_source_get_private_settings(scene);
		obs_data_set_default_int(data, "transition_duration", 300);
		const std::string current = obs_data_get_string(data, "transition");
		const int curDuration = (int)obs_data_get_int(data, "transition_duration");
		obs_data_release(data);
		obs_source_release(scene);

		QMenu *sub = menu.addMenu(obs_module_text("BSD.TransitionOverride"));

		/* Embedded duration spinbox (same as OBS' native per-scene menu). */
		auto *durRow = new QWidget(sub);
		auto *durLayout = new QHBoxLayout(durRow);
		durLayout->setContentsMargins(8, 2, 8, 2);
		durLayout->addWidget(new QLabel(obs_module_text("BSD.Duration"), durRow));
		auto *durSpin = new QSpinBox(durRow);
		durSpin->setMinimum(50);
		durSpin->setMaximum(20000);
		durSpin->setSingleStep(50);
		durSpin->setSuffix(QStringLiteral(" ms"));
		durSpin->setValue(curDuration);
		connect(durSpin, &QSpinBox::valueChanged, this,
			[this, sceneName](int v) { setTransitionDuration(sceneName, v); });
		durLayout->addWidget(durSpin);
		auto *durAction = new QWidgetAction(sub);
		durAction->setDefaultWidget(durRow);
		sub->addAction(durAction);
		sub->addSeparator();

		auto addItem = [&](const QString &label, const std::string &transition) {
			QAction *a = sub->addAction(label, this, [this, sceneName, transition]() {
				setTransitionOverride(sceneName, transition);
			});
			a->setCheckable(true);
			a->setChecked(transition == current);
		};
		addItem(obs_module_text("BSD.None"), std::string());

		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			const char *tn = obs_source_get_name(transitions.sources.array[i]);
			addItem(QString::fromUtf8(tn), tn);
		}
		obs_frontend_source_list_free(&transitions);
	}

	void setTransitionOverride(const std::string &sceneName, const std::string &transition)
	{
		obs_source_t *scene = obs_get_source_by_name(sceneName.c_str());
		if (!scene)
			return;
		obs_data_t *data = obs_source_get_private_settings(scene);
		obs_data_set_string(data, "transition", transition.c_str());
		obs_data_release(data);
		obs_source_release(scene);
	}

	void setTransitionDuration(const std::string &sceneName, int duration)
	{
		obs_source_t *scene = obs_get_source_by_name(sceneName.c_str());
		if (!scene)
			return;
		obs_data_t *data = obs_source_get_private_settings(scene);
		obs_data_set_int(data, "transition_duration", duration);
		obs_data_release(data);
		obs_source_release(scene);
	}

	void pasteFilters(const std::string &targetName)
	{
		if (copiedFiltersSource_.empty())
			return;
		obs_source_t *src = obs_get_source_by_name(copiedFiltersSource_.c_str());
		obs_source_t *dst = obs_get_source_by_name(targetName.c_str());
		if (src && dst && src != dst)
			obs_source_copy_filters(dst, src);
		obs_source_release(src);
		obs_source_release(dst);
	}

	void duplicateScene(Node *n)
	{
		obs_source_t *src = obs_get_source_by_name(n->name.c_str());
		if (!src)
			return;
		obs_scene_t *scene = obs_scene_from_source(src);
		if (!scene) {
			obs_source_release(src);
			return;
		}
		const std::string name = uniqueSceneName(n->name);
		const auto dupType = settings_.duplicateCopies ? OBS_SCENE_DUP_COPY : OBS_SCENE_DUP_REFS;
		obs_scene_t *dup = obs_scene_duplicate(scene, name.c_str(), dupType);
		obs_source_release(src);
		if (!dup)
			return;
		obs_scene_release(dup);

		/* Place the copy right after the original (the queued SCENE_LIST_CHANGED
		 * would otherwise just append it at the root). */
		model_.reconcile_with_obs();
		Node *newNode = model_.find(name);
		Node *parent = model_.find_parent(n);
		if (newNode && parent) {
			int idx = (int)parent->children.size();
			for (int i = 0; i < (int)parent->children.size(); i++) {
				if (parent->children[i].get() == n) {
					idx = i + 1;
					break;
				}
			}
			model_.move_node(newNode, parent, idx);
		}
		save();
		rebuild();
		selectNodeById(name);
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
		const QString input = QInputDialog::getText(this, obs_module_text("BSD.Rename"),
							    obs_module_text("BSD.RenamePrompt"), QLineEdit::Normal,
							    QString::fromStdString(n->name), &ok);
		if (!ok)
			return;
		const std::string newName = input.trimmed().toStdString();
		if (newName.empty() || newName == n->name)
			return;

		if (n->type == NodeType::Scene) {
			if (obs_source_t *clash = obs_get_source_by_name(newName.c_str())) {
				obs_source_release(clash);
				QMessageBox::warning(this, obs_module_text("BSD.Rename"),
						     obs_module_text("BSD.NameExists"));
				return;
			}
			obs_source_t *src = obs_get_source_by_name(n->name.c_str());
			if (!src)
				return;
			obs_source_set_name(src, newName.c_str()); // fires no SCENE_LIST_CHANGED
			obs_source_release(src);
			/* Keep the node in place (id == scene name for scenes). */
			n->id = newName;
		}
		n->name = newName;
		save();
		rebuild();
		selectNodeById(n->id);
	}

	/* libobs global "source_rename" hook — fires for any source on any thread.
	 * Marshal to the GUI thread and keep the scene's folder placement. */
	static void onSourceRename(void *data, calldata_t *cd)
	{
		auto *self = static_cast<BetterScenesDock *>(data);
		obs_source_t *src = (obs_source_t *)calldata_ptr(cd, "source");
		if (!src || obs_source_get_type(src) != OBS_SOURCE_TYPE_SCENE)
			return;
		const char *prev = calldata_string(cd, "prev_name");
		const char *next = calldata_string(cd, "new_name");
		if (!prev || !next || prev == std::string(next))
			return;
		std::string p = prev, n = next;
		QMetaObject::invokeMethod(
			self, [self, p, n]() { self->handleExternalRename(p, n); }, Qt::QueuedConnection);
	}

	void handleExternalRename(const std::string &prev, const std::string &next)
	{
		Node *node = model_.find(prev); // scene id == scene name
		if (!node || node->type != NodeType::Scene)
			return; // our own in-dock rename already updated the node
		node->id = next;
		node->name = next;
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

	void applyColor(Node *n, const QColor &c)
	{
		n->color = c.isValid() ? c.name().toStdString() : std::string();
		save();
		rebuild();
	}

	/* A preset swatch row + Custom…/Clear, shared by the toolbar colour button
	 * and the context menu. */
	void buildColorMenu(QMenu *menu, Node *n)
	{
		static const char *presets[] = {"#e6433b", "#e67e22", "#f1c40f", "#2ecc71",
						"#1abc9c", "#3a8fe8", "#9b59b6", "#e84393"};
		auto *row = new QWidget(menu);
		auto *hl = new QHBoxLayout(row);
		hl->setContentsMargins(6, 4, 6, 4);
		hl->setSpacing(4);
		for (const char *hex : presets) {
			const QColor c(hex);
			auto *b = new QToolButton(row);
			b->setAutoRaise(true);
			b->setFocusPolicy(Qt::NoFocus);
			b->setIconSize(QSize(kIconPx, kIconPx));
			b->setIcon(swatchIcon(c));
			b->setToolTip(QString::fromUtf8(hex));
			connect(b, &QToolButton::clicked, this, [this, n, c, menu]() {
				applyColor(n, c);
				menu->close();
			});
			hl->addWidget(b);
		}
		auto *wa = new QWidgetAction(menu);
		wa->setDefaultWidget(row);
		menu->addAction(wa);
		menu->addSeparator();
		menu->addAction(obs_module_text("BSD.CustomColor"), this, [this, n]() { setColor(n); });
		if (!n->color.empty())
			menu->addAction(obs_module_text("BSD.ClearColor"), this,
					[this, n]() { applyColor(n, QColor()); });
	}

	/* ---- toolbar actions ---- */

	std::string uniqueSceneName(const std::string &base)
	{
		std::string name = base;
		int n = 2;
		for (;;) {
			obs_source_t *s = obs_get_source_by_name(name.c_str());
			if (!s)
				return name;
			obs_source_release(s);
			name = base + " " + std::to_string(n++);
		}
	}

	void newScene()
	{
		bool ok = false;
		const QString input = QInputDialog::getText(this, obs_module_text("BSD.NewScene"),
							    obs_module_text("BSD.SceneNamePrompt"), QLineEdit::Normal,
							    obs_module_text("BSD.SceneDefault"), &ok);
		if (!ok)
			return;
		std::string base = input.trimmed().toStdString();
		if (base.empty())
			base = obs_module_text("BSD.SceneDefault");
		const std::string name = uniqueSceneName(base);

		obs_scene_t *scene = obs_scene_create(name.c_str());
		if (!scene)
			return;
		obs_scene_release(scene);

		/* Pull the new scene into the model now (the frontend event is
		 * queued) and place it right after the current selection. */
		model_.reconcile_with_obs();
		if (Node *sel = selectedNode()) {
			Node *newNode = model_.find(name);
			Node *parent = sel->is_container() ? sel : model_.find_parent(sel);
			if (newNode && parent) {
				int idx = (int)parent->children.size();
				for (int i = 0; i < (int)parent->children.size(); i++) {
					if (parent->children[i].get() == sel) {
						idx = i + 1;
						break;
					}
				}
				model_.move_node(newNode, parent, idx);
			}
		}
		save();
		rebuild();
		selectNodeById(name);
	}

	void removeSelected() { removeNode(selectedNode()); }

	void removeNode(Node *n)
	{
		if (!n)
			return;

		if (n->type == NodeType::Scene) {
			obs_source_t *scene = obs_get_source_by_name(n->name.c_str());
			if (!scene)
				return;
			const auto reply = QMessageBox::question(
				this, obs_module_text("BSD.Remove"),
				QString(obs_module_text("BSD.ConfirmRemoveScene")).arg(QString::fromStdString(n->name)));
			if (reply == QMessageBox::Yes)
				obs_source_remove(scene); // SCENE_LIST_CHANGED -> reconcile + rebuild
			obs_source_release(scene);
		} else {
			model_.remove_node(n);
			save();
			rebuild();
		}
	}

	void openFilters()
	{
		Node *n = selectedNode();
		if (!n || n->type != NodeType::Scene)
			return;
		obs_source_t *scene = obs_get_source_by_name(n->name.c_str());
		if (scene) {
			obs_frontend_open_source_filters(scene);
			obs_source_release(scene);
		}
	}

	void moveSelected(int delta)
	{
		Node *n = selectedNode();
		if (!n)
			return;
		const std::string id = n->id;
		if (model_.move_within_parent(n, delta)) {
			save();
			rebuild();
			selectNodeById(id);
		}
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
