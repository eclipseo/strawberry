/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QtGlobal>
#include <QWidget>
#include <QVariant>
#include <QStringList>
#include <QAction>
#include <QHeaderView>
#include <QMessageBox>
#include <QProcess>
#include <QTreeWidget>
#include <QKeySequence>
#include <QShortcut>
#include <QSettings>
#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QRadioButton>

#include "core/iconloader.h"
#include "core/utilities.h"
#include "globalshortcuts/globalshortcutgrabber.h"
#include "globalshortcuts/globalshortcuts.h"
#include "settingspage.h"
#include "settingsdialog.h"
#include "shortcutssettingspage.h"
#include "ui_shortcutssettingspage.h"

const char *GlobalShortcutsSettingsPage::kSettingsGroup = "GlobalShortcuts";

GlobalShortcutsSettingsPage::GlobalShortcutsSettingsPage(SettingsDialog *dialog)
    : SettingsPage(dialog),
      ui_(new Ui_GlobalShortcutsSettingsPage),
      initialised_(false),
      grabber_(new GlobalShortcutGrabber) {
  ui_->setupUi(this);
  ui_->shortcut_options->setEnabled(false);
  ui_->list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  setWindowIcon(IconLoader::Load("keyboard"));

  settings_.beginGroup(kSettingsGroup);

  connect(ui_->list, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)), SLOT(ItemClicked(QTreeWidgetItem*)));
  connect(ui_->radio_none, SIGNAL(clicked()), SLOT(NoneClicked()));
  connect(ui_->radio_default, SIGNAL(clicked()), SLOT(DefaultClicked()));
  connect(ui_->radio_custom, SIGNAL(clicked()), SLOT(ChangeClicked()));
  connect(ui_->change, SIGNAL(clicked()), SLOT(ChangeClicked()));
  connect(ui_->gnome_open, SIGNAL(clicked()), SLOT(OpenGnomeKeybindingProperties()));

}

GlobalShortcutsSettingsPage::~GlobalShortcutsSettingsPage() { delete ui_; }

bool GlobalShortcutsSettingsPage::IsEnabled() const {

#ifdef Q_OS_MACOS
  qLog(Debug) << Utilities::GetMacVersion();
  if (Utilities::GetMacVersion() < 6) {  // Leopard and earlier.
    return false;
  }
#endif

  return true;

}

void GlobalShortcutsSettingsPage::Load() {

  GlobalShortcuts *manager = dialog()->global_shortcuts_manager();

  if (!initialised_) {
    initialised_ = true;

    connect(ui_->mac_open, SIGNAL(clicked()), manager, SLOT(ShowMacAccessibilityDialog()));

    if (!manager->IsGsdAvailable()) {
      ui_->gnome_container->hide();
    }

    for (const GlobalShortcuts::Shortcut &s : manager->shortcuts().values()) {
      Shortcut shortcut;
      shortcut.s = s;
      shortcut.key = s.action->shortcut();
      shortcut.item = new QTreeWidgetItem(ui_->list, QStringList() << s.action->text() << s.action->shortcut().toString(QKeySequence::NativeText));
      shortcut.item->setData(0, Qt::UserRole, s.id);
      shortcuts_[s.id] = shortcut;
    }

    ui_->list->sortItems(0, Qt::AscendingOrder);
    ItemClicked(ui_->list->topLevelItem(0));
  }

  for (const Shortcut &s : shortcuts_.values()) {
    SetShortcut(s.s.id, s.s.action->shortcut());
  }

  bool use_gnome = settings_.value("use_gnome", true).toBool();
  if (ui_->gnome_container->isVisibleTo(this)) {
    ui_->gnome_checkbox->setChecked(use_gnome);
  }

  ui_->mac_container->setVisible(!manager->IsMacAccessibilityEnabled());
#ifdef Q_OS_MACOS
  qint32 mac_version = Utilities::GetMacVersion();
  ui_->mac_label->setVisible(mac_version < 9);
  ui_->mac_label_mavericks->setVisible(mac_version >= 9);
#endif  // Q_OS_MACOS

}

void GlobalShortcutsSettingsPage::SetShortcut(const QString &id, const QKeySequence &key) {

  Shortcut &shortcut = shortcuts_[id];

  shortcut.key = key;
  shortcut.item->setText(1, key.toString(QKeySequence::NativeText));

}

void GlobalShortcutsSettingsPage::Save() {

  for (const Shortcut &s : shortcuts_.values()) {
    s.s.action->setShortcut(s.key);
    s.s.shortcut->setKey(s.key);
    settings_.setValue(s.s.id, s.key.toString());
  }

  settings_.setValue("use_gnome", ui_->gnome_checkbox->isChecked());

  dialog()->global_shortcuts_manager()->ReloadSettings();

}

void GlobalShortcutsSettingsPage::ItemClicked(QTreeWidgetItem *item) {

  current_id_ = item->data(0, Qt::UserRole).toString();
  Shortcut &shortcut = shortcuts_[current_id_];

  // Enable options
  ui_->shortcut_options->setEnabled(true);
  ui_->shortcut_options->setTitle(tr("Shortcut for %1").arg(shortcut.s.action->text()));

  if (shortcut.key == shortcut.s.default_key)
    ui_->radio_default->setChecked(true);
  else if (shortcut.key.isEmpty())
    ui_->radio_none->setChecked(true);
  else
    ui_->radio_custom->setChecked(true);

}

void GlobalShortcutsSettingsPage::NoneClicked() {
  SetShortcut(current_id_, QKeySequence());
}

void GlobalShortcutsSettingsPage::DefaultClicked() {
  SetShortcut(current_id_, shortcuts_[current_id_].s.default_key);
}

void GlobalShortcutsSettingsPage::ChangeClicked() {

  GlobalShortcuts *manager = dialog()->global_shortcuts_manager();
  manager->Unregister();
  QKeySequence key = grabber_->GetKey(shortcuts_[current_id_].s.action->text());
  manager->Register();

  if (key.isEmpty()) return;

  // Check if this key sequence is used by any other actions
  for (const QString &id : shortcuts_.keys()) {
    if (shortcuts_[id].key == key) SetShortcut(id, QKeySequence());
  }

  ui_->radio_custom->setChecked(true);
  SetShortcut(current_id_, key);

}

void GlobalShortcutsSettingsPage::OpenGnomeKeybindingProperties() {

  if (!QProcess::startDetached("gnome-keybinding-properties")) {
    if (!QProcess::startDetached("gnome-control-center", QStringList() << "keyboard")) {
      QMessageBox::warning(this, "Error",
                           tr("The \"%1\" command could not be started.")
                               .arg("gnome-keybinding-properties"));
    }
  }

}
