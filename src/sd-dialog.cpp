/*
Scene Dividers — Labeled dividers for the OBS scene list
Copyright (C) 2026 Paul Loth <mail@paulloth.de>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. See <https://www.gnu.org/licenses/>.
*/

/*
 * sd-dialog.cpp — management dialog (Tools menu → "Scene Dividers…").
 *
 * Divider rows are unselectable in the native scene list (on purpose), so
 * everything that needs a selected divider lives here: add at a position,
 * rename, accent color, remove, move up/down. The dialog mirrors the native
 * list row-by-row; dialog row index == native row index.
 */

#include "sd-dialog.hpp"
#include "sd-dividers.hpp"
#include "sd-scene-list.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QColorDialog>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace sd {

class DividersDialog : public QDialog {
public:
	explicit DividersDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle(obs_module_text("SceneDividers.Dialog.Title"));
		setAttribute(Qt::WA_DeleteOnClose);
		resize(380, 480);

		list_ = new QListWidget(this);
		list_->setSelectionMode(QAbstractItemView::SingleSelection);

		addBtn_ = new QPushButton(obs_module_text("SceneDividers.Add"), this);
		renameBtn_ = new QPushButton(obs_module_text("SceneDividers.Rename"), this);
		colorBtn_ = new QPushButton(obs_module_text("SceneDividers.Color"), this);
		removeBtn_ = new QPushButton(obs_module_text("SceneDividers.Remove"), this);
		upBtn_ = new QPushButton(QStringLiteral("▲"), this);
		downBtn_ = new QPushButton(QStringLiteral("▼"), this);
		upBtn_->setFixedWidth(40);
		downBtn_->setFixedWidth(40);

		auto *hint = new QLabel(obs_module_text("SceneDividers.Hint"), this);
		hint->setWordWrap(true);

		auto *row1 = new QHBoxLayout();
		row1->addWidget(addBtn_);
		row1->addWidget(renameBtn_);
		row1->addWidget(colorBtn_);
		auto *row2 = new QHBoxLayout();
		row2->addWidget(removeBtn_);
		row2->addStretch();
		row2->addWidget(upBtn_);
		row2->addWidget(downBtn_);

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(hint);
		layout->addWidget(list_);
		layout->addLayout(row1);
		layout->addLayout(row2);

		connect(list_, &QListWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
		connect(addBtn_, &QPushButton::clicked, this, [this]() { addDivider(); });
		connect(renameBtn_, &QPushButton::clicked, this, [this]() { renameDivider(); });
		connect(colorBtn_, &QPushButton::clicked, this, [this]() { pickColor(); });
		connect(removeBtn_, &QPushButton::clicked, this, [this]() { removeDivider(); });
		connect(upBtn_, &QPushButton::clicked, this, [this]() { moveBy(-1); });
		connect(downBtn_, &QPushButton::clicked, this, [this]() { moveBy(+1); });

		obs_frontend_add_event_callback(frontendEvent, this);
		refresh();
	}

	~DividersDialog() override { obs_frontend_remove_event_callback(frontendEvent, this); }

	void refresh()
	{
		const QString remembered = list_->currentItem() ? list_->currentItem()->text() : QString();
		list_->clear();

		QListWidget *scenes = scene_list_widget();
		if (!scenes)
			return;
		for (int i = 0; i < scenes->count(); i++) {
			const QString name = scenes->item(i)->text();
			auto *item = new QListWidgetItem(name, list_);
			if (is_divider_by_name(name.toUtf8().constData())) {
				item->setData(Qt::UserRole, true);
				QFont f = item->font();
				f.setBold(true);
				item->setFont(f);
				const QColor c(QString::fromStdString(divider_color_by_name(name.toUtf8().constData())));
				if (c.isValid())
					item->setForeground(c);
			}
			if (!remembered.isEmpty() && name == remembered)
				list_->setCurrentItem(item);
		}
		updateButtons();
	}

private:
	QListWidget *list_;
	QPushButton *addBtn_, *renameBtn_, *colorBtn_, *removeBtn_, *upBtn_, *downBtn_;

	int selectedRow() const { return list_->currentRow(); }

	bool selectedIsDivider() const
	{
		QListWidgetItem *item = list_->currentItem();
		return item && item->data(Qt::UserRole).toBool();
	}

	void updateButtons()
	{
		const bool divider = selectedIsDivider();
		renameBtn_->setEnabled(divider);
		colorBtn_->setEnabled(divider);
		removeBtn_->setEnabled(divider);
		upBtn_->setEnabled(divider && selectedRow() > 0);
		downBtn_->setEnabled(divider && selectedRow() >= 0 && selectedRow() < list_->count() - 1);
	}

	void refreshSoon()
	{
		/* Some frontend reactions (item removal/rename) may be queued;
		 * refresh after the event loop has processed them. */
		QTimer::singleShot(0, this, [this]() { refresh(); });
	}

	void addDivider()
	{
		bool ok = false;
		const QString label = QInputDialog::getText(this, obs_module_text("SceneDividers.Dialog.Title"),
							    obs_module_text("SceneDividers.LabelPrompt"),
							    QLineEdit::Normal, QString(), &ok);
		if (!ok)
			return;

		const int target = selectedRow();
		const std::string name = create_divider(label.trimmed().toUtf8().constData());
		if (name.empty())
			return;

		/* The new scene row exists now (AddScene runs synchronously on
		 * this thread); move it from "below current scene" to the row
		 * selected in this dialog, if any. */
		QListWidget *scenes = scene_list_widget();
		if (scenes && target >= 0) {
			const QString qname = QString::fromStdString(name);
			for (int i = 0; i < scenes->count(); i++) {
				if (scenes->item(i)->text() == qname) {
					if (i != target)
						move_scene_row(i, target);
					break;
				}
			}
		}
		restyle_scene_list();
		obs_frontend_save();
		refreshSoon();
	}

	void renameDivider()
	{
		if (!selectedIsDivider())
			return;
		const QString oldName = list_->currentItem()->text();
		bool ok = false;
		const QString newName = QInputDialog::getText(this, obs_module_text("SceneDividers.Dialog.Title"),
							      obs_module_text("SceneDividers.RenamePrompt"),
							      QLineEdit::Normal, oldName, &ok);
		if (!ok || newName.trimmed().isEmpty() || newName == oldName)
			return;
		rename_divider(oldName.toUtf8().constData(), newName.trimmed().toUtf8().constData());
		restyle_scene_list();
		obs_frontend_save();
		refreshSoon();
	}

	void pickColor()
	{
		if (!selectedIsDivider())
			return;
		const QString name = list_->currentItem()->text();
		QColor initial(QString::fromStdString(divider_color_by_name(name.toUtf8().constData())));
		const QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::gray), this,
							    obs_module_text("SceneDividers.Color"));
		if (!color.isValid())
			return;
		set_divider_color_by_name(name.toUtf8().constData(), color.name().toUtf8().constData());
		restyle_scene_list();
		obs_frontend_save();
		refreshSoon();
	}

	void removeDivider()
	{
		if (!selectedIsDivider())
			return;
		remove_divider_by_name(list_->currentItem()->text().toUtf8().constData());
		refreshSoon();
	}

	void moveBy(int delta)
	{
		if (!selectedIsDivider())
			return;
		const int row = selectedRow();
		if (move_scene_row(row, row + delta)) {
			refresh();
			list_->setCurrentRow(row + delta);
		}
	}

	static void frontendEvent(enum obs_frontend_event event, void *data)
	{
		if (event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED ||
		    event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
			auto *dialog = static_cast<DividersDialog *>(data);
			QMetaObject::invokeMethod(dialog, [dialog]() { dialog->refresh(); }, Qt::QueuedConnection);
		}
	}
};

static QPointer<DividersDialog> s_dialog;

static void show_dialog()
{
	if (!s_dialog) {
		auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		s_dialog = new DividersDialog(main);
	}
	s_dialog->refresh();
	s_dialog->show();
	s_dialog->raise();
	s_dialog->activateWindow();
}

void dialog_register_menu()
{
	obs_frontend_add_tools_menu_item(obs_module_text("SceneDividers.MenuItem"), [](void *) { show_dialog(); },
					 nullptr);
}

void dialog_close()
{
	if (s_dialog)
		delete s_dialog;
}

} // namespace sd
