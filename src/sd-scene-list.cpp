/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

/*
 * sd-scene-list.cpp — the Qt hook into OBS' native scene list.
 *
 * OBS keeps the scene list in a QListWidget (class SceneTree, objectName
 * "scenes"). Every row is a real scene; the row order is what OBS saves as
 * "scene_order". We do not insert foreign rows — divider scenes are real
 * (empty) scenes, we merely restyle their rows:
 *
 *   - a custom item delegate paints them as a line with a centered label
 *   - Qt::ItemIsSelectable/Editable is cleared so clicking a divider can
 *     never switch the program scene (on_scenes_currentItemChanged)
 *
 * OBS rebuilds/extends the list on scene events, so the styling pass reruns
 * on the relevant frontend events and on model rowsInserted.
 */

#include "sd-scene-list.hpp"
#include "sd-dividers.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAbstractItemModel>
#include <QListWidget>
#include <QMainWindow>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QStyledItemDelegate>

namespace sd {

/* Divider state cached on the items (written by restyle_scene_list, read by
 * the delegate). Qt::UserRole and UserRole+1 are taken by OBS (OBSRef /
 * OBSSignals), so start well above them. */
enum {
	RoleIsDivider = Qt::UserRole + 100,
	RoleColor,
};

class DividerDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
	{
		if (!index.data(RoleIsDivider).toBool()) {
			QStyledItemDelegate::paint(p, opt, index);
			return;
		}

		/* Strip leading/trailing dash-like padding so an adopted manual
		 * divider ("------ME1------") shows a clean centered label. */
		QString name = index.data(Qt::DisplayRole).toString().trimmed();
		const bool lineOnly = label_is_plain_line(name.toUtf8().constData());
		if (!lineOnly) {
			while (!name.isEmpty() && (name.front() == '-' || name.front() == '_' ||
						   name.front() == QChar(0x2500) || name.front() == QChar(0x2014) ||
						   name.front() == ' '))
				name.remove(0, 1);
			while (!name.isEmpty() && (name.back() == '-' || name.back() == '_' ||
						   name.back() == QChar(0x2500) || name.back() == QChar(0x2014) ||
						   name.back() == ' '))
				name.chop(1);
		}

		QColor color(index.data(RoleColor).toString());
		const bool hasAccent = color.isValid();
		if (!hasAccent)
			color = opt.palette.color(QPalette::Disabled, QPalette::Text);

		p->save();
		p->setRenderHint(QPainter::Antialiasing, true);

		/* Subtle full-row background so a divider reads as a separator,
		 * not as a clickable scene. Tinted toward the accent if set. */
		QColor bg = hasAccent ? color : opt.palette.color(QPalette::Highlight);
		bg.setAlpha(hasAccent ? 38 : 28);
		p->fillRect(opt.rect, bg);

		p->setPen(QPen(color, lineOnly ? 2 : 1));

		const QRect r = opt.rect.adjusted(8, 0, -8, 0);
		const int y = r.center().y();

		if (lineOnly) {
			p->drawLine(r.left(), y, r.right(), y);
		} else {
			QFont f = opt.font;
			f.setBold(true);
			f.setCapitalization(QFont::AllUppercase);
			if (f.pointSizeF() > 0)
				f.setPointSizeF(f.pointSizeF() * 0.9);
			p->setFont(f);

			const QFontMetrics fm(f);
			const QString label = fm.elidedText(name, Qt::ElideRight, r.width() - 24);
			const int tw = fm.horizontalAdvance(label);
			const int tx = r.center().x() - tw / 2;

			if (tx - 8 > r.left())
				p->drawLine(r.left(), y, tx - 8, y);
			if (tx + tw + 8 < r.right())
				p->drawLine(tx + tw + 8, y, r.right(), y);
			p->setPen(hasAccent ? color : opt.palette.color(QPalette::Text));
			p->drawText(QRect(tx, opt.rect.top(), tw, opt.rect.height()), Qt::AlignCenter, label);
		}
		p->restore();
	}
};

static QPointer<QListWidget> s_list;
static QPointer<DividerDelegate> s_delegate;
static QAbstractItemDelegate *s_prev_delegate = nullptr;
static bool s_restyle_pending = false;

QListWidget *scene_list_widget()
{
	return s_list;
}

void restyle_scene_list()
{
	if (!s_list)
		return;
	for (int i = 0; i < s_list->count(); i++) {
		QListWidgetItem *item = s_list->item(i);
		obs_source_t *source = obs_get_source_by_name(item->text().toUtf8().constData());
		const bool divider = is_divider(source);
		if (divider) {
			item->setData(RoleIsDivider, true);
			item->setData(RoleColor, QString::fromStdString(divider_color(source)));
			item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEditable));
		} else if (item->data(RoleIsDivider).toBool()) {
			/* Row stopped being a divider (should not happen, but
			 * stay consistent if e.g. names were swapped). */
			item->setData(RoleIsDivider, false);
			item->setFlags(item->flags() | Qt::ItemIsSelectable);
		}
		obs_source_release(source);
	}
}

void schedule_restyle()
{
	if (!s_list || s_restyle_pending)
		return;
	s_restyle_pending = true;
	QMetaObject::invokeMethod(
		s_list,
		[]() {
			s_restyle_pending = false;
			restyle_scene_list();
		},
		Qt::QueuedConnection);
}

bool move_scene_row(int from, int to)
{
	if (!s_list || from == to)
		return false;
	if (from < 0 || from >= s_list->count() || to < 0 || to >= s_list->count())
		return false;

	/* Dividers are never the current item (unselectable), so taking the
	 * row does not trigger on_scenes_currentItemChanged. */
	QListWidgetItem *item = s_list->takeItem(from);
	s_list->insertItem(to, item);

	/* Same follow-up as a user drag: SceneTree::scenesReordered updates
	 * multiview projectors; saving persists the new "scene_order". */
	QMetaObject::invokeMethod(s_list, "scenesReordered");
	obs_frontend_save();
	return true;
}

void refresh_multiview()
{
	if (s_list)
		QMetaObject::invokeMethod(s_list, "scenesReordered");
}

static void unhook_scene_list()
{
	if (s_list && s_delegate) {
		s_list->setItemDelegate(s_prev_delegate);
		delete s_delegate;
	}
	s_prev_delegate = nullptr;
	s_list = nullptr;
}

static void hook_scene_list()
{
	if (s_list)
		return;
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main)
		return;
	s_list = main->findChild<QListWidget *>("scenes");
	if (!s_list) {
		obs_log(LOG_WARNING, "scene list widget 'scenes' not found — dividers will look like plain scenes");
		return;
	}

	s_prev_delegate = s_list->itemDelegate();
	s_delegate = new DividerDelegate(s_list);
	s_list->setItemDelegate(s_delegate);

	/* OBS inserts rows on scene add and on full list rebuilds; new items
	 * start out selectable until the next styling pass. */
	QObject::connect(s_list->model(), &QAbstractItemModel::rowsInserted, s_delegate, []() { schedule_restyle(); });

	restyle_scene_list();
	obs_log(LOG_INFO, "scene list hooked (divider delegate installed)");
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		hook_scene_list();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		schedule_restyle();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		unhook_scene_list();
		break;
	default:
		break;
	}
}

void scene_list_init()
{
	obs_frontend_add_event_callback(frontend_event, nullptr);
}

void scene_list_shutdown()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	unhook_scene_list();
}

} // namespace sd
