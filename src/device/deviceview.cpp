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

#include <memory>

#include <QtGlobal>
#include <QApplication>
#include <QObject>
#include <QWidget>
#include <QAbstractItemView>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QStyleOptionViewItem>
#include <QMimeData>
#include <QAction>
#include <QVariant>
#include <QUrl>
#include <QFont>
#include <QFontMetrics>
#include <QString>
#include <QStringList>
#include <QPixmap>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QStyle>
#include <QMenu>
#include <QFlags>
#include <QPushButton>
#include <QMessageBox>
#include <QtEvents>

#include "core/logging.h"
#include "core/iconloader.h"
#include "core/application.h"
#include "core/deletefiles.h"
#include "core/mergedproxymodel.h"
#include "core/mimedata.h"
#include "core/musicstorage.h"
#include "dialogs/organisedialog.h"
#include "dialogs/organiseerrordialog.h"
#include "collection/collectiondirectorymodel.h"
#include "collection/collectionmodel.h"
#include "collection/collectionview.h"
#include "connecteddevice.h"
#include "devicelister.h"
#include "devicemanager.h"
#include "deviceproperties.h"
#include "deviceview.h"

const int DeviceItemDelegate::kIconPadding = 6;

DeviceItemDelegate::DeviceItemDelegate(QObject *parent) : CollectionItemDelegate(parent) {}

void DeviceItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const {

  // Is it a device or a collection item?
  if (index.data(DeviceManager::Role::Role_State).isNull()) {
    CollectionItemDelegate::paint(p, opt, index);
    return;
  }

  // Draw the background
  const QStyleOptionViewItem *vopt = qstyleoption_cast<const QStyleOptionViewItem*>(&opt);
  const QWidget *widget = vopt->widget;
  QStyle *style = widget->style() ? widget->style() : QApplication::style();
  style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, widget);

  p->save();

  // Font for the status line
  QFont status_font(opt.font);

#ifdef Q_OS_WIN32
  status_font.setPointSize(status_font.pointSize() - 1);
#else
  status_font.setPointSize(status_font.pointSize() - 2);
#endif

  const int text_height = QFontMetrics(opt.font).height() + QFontMetrics(status_font).height();

  QRect line1(opt.rect);
  QRect line2(opt.rect);
  line1.setTop(line1.top() + (opt.rect.height() - text_height) / 2);
  line2.setTop(line1.top() + QFontMetrics(opt.font).height());
  line1.setLeft(line1.left() + DeviceManager::kDeviceIconSize + kIconPadding);
  line2.setLeft(line2.left() + DeviceManager::kDeviceIconSize + kIconPadding);

  // Change the color for selected items
  if (opt.state & QStyle::State_Selected) {
    p->setPen(opt.palette.color(QPalette::HighlightedText));
  }

  // Draw the icon
  p->drawPixmap(opt.rect.topLeft(), index.data(Qt::DecorationRole).value<QPixmap>());

  // Draw the first line (device name)
  p->drawText(line1, Qt::AlignLeft | Qt::AlignTop, index.data().toString());

  // Draw the second line (status)
  DeviceManager::State state = static_cast<DeviceManager::State>(index.data(DeviceManager::Role_State).toInt());
  QVariant progress = index.data(DeviceManager::Role_UpdatingPercentage);
  QString status_text;

  if (progress.isValid()) {
    status_text = tr("Updating %1%...").arg(progress.toInt());
  }
  else {
    switch (state) {
      case DeviceManager::State_Remembered:
        status_text = tr("Not connected");
        break;

      case DeviceManager::State_NotMounted:
        status_text = tr("Not mounted - double click to mount");
        break;

      case DeviceManager::State_NotConnected:
        status_text = tr("Double click to open");
        break;

      case DeviceManager::State_Connected: {
        QVariant song_count = index.data(DeviceManager::Role_SongCount);
        if (song_count.isValid()) {
          int count = song_count.toInt();
          if (count == 1)  // TODO: Fix this properly
            status_text = tr("%1 song").arg(count);
          else
            status_text = tr("%1 songs").arg(count);
        }
        else {
          status_text = index.data(DeviceManager::Role_MountPath).toString();
        }
        break;
      }
    }
  }

  if (opt.state & QStyle::State_Selected)
    p->setPen(opt.palette.color(QPalette::HighlightedText));
  else
    p->setPen(opt.palette.color(QPalette::Dark));
  p->setFont(status_font);
  p->drawText(line2, Qt::AlignLeft | Qt::AlignTop, status_text);

  p->restore();

}

DeviceView::DeviceView(QWidget *parent)
    : AutoExpandingTreeView(parent),
      app_(nullptr),
      merged_model_(nullptr),
      sort_model_(nullptr),
      properties_dialog_(new DeviceProperties),
      device_menu_(nullptr),
      collection_menu_(nullptr) {
  setItemDelegate(new DeviceItemDelegate(this));
  SetExpandOnReset(false);
  setAttribute(Qt::WA_MacShowFocusRect, false);
  setHeaderHidden(true);
  setAllColumnsShowFocus(true);
  setDragEnabled(true);
  setDragDropMode(QAbstractItemView::DragOnly);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
}

DeviceView::~DeviceView() {}

void DeviceView::SetApplication(Application *app) {

  Q_ASSERT(app_ == nullptr);
  app_ = app;

  connect(app_->device_manager(), SIGNAL(DeviceConnected(int)), SLOT(DeviceConnected(int)));
  connect(app_->device_manager(), SIGNAL(DeviceDisconnected(int)), SLOT(DeviceDisconnected(int)));

  sort_model_ = new QSortFilterProxyModel(this);
  sort_model_->setSourceModel(app_->device_manager());
  sort_model_->setDynamicSortFilter(true);
  sort_model_->setSortCaseSensitivity(Qt::CaseInsensitive);
  sort_model_->sort(0);

  merged_model_ = new MergedProxyModel(this);
  merged_model_->setSourceModel(sort_model_);
  setModel(merged_model_);

  connect(merged_model_, SIGNAL(SubModelReset(QModelIndex, QAbstractItemModel*)), SLOT(RecursivelyExpand(QModelIndex)));

  properties_dialog_->SetDeviceManager(app_->device_manager());

#ifdef HAVE_GSTREAMER
  organise_dialog_.reset(new OrganiseDialog(app_->task_manager()));
  organise_dialog_->SetDestinationModel(app_->collection_model()->directory_model());
#endif

}

void DeviceView::contextMenuEvent(QContextMenuEvent *e) {

  if (!device_menu_) {
    device_menu_ = new QMenu(this);
    collection_menu_ = new QMenu(this);

    // Device menu
    eject_action_ = device_menu_->addAction(IconLoader::Load("media-eject"), tr("Safely remove device"), this, SLOT(Unmount()));
    forget_action_ = device_menu_->addAction(IconLoader::Load("list-remove"), tr("Forget device"), this, SLOT(Forget()));
    device_menu_->addSeparator();
    properties_action_ = device_menu_->addAction(IconLoader::Load("configure"), tr("Device properties..."), this, SLOT(Properties()));

    // Collection menu
    add_to_playlist_action_ = collection_menu_->addAction(IconLoader::Load("media-play"), tr("Append to current playlist"), this, SLOT(AddToPlaylist()));
    load_action_ = collection_menu_->addAction(IconLoader::Load("media-play"), tr("Replace current playlist"), this, SLOT(Load()));
    open_in_new_playlist_ = collection_menu_->addAction(IconLoader::Load("document-new"), tr("Open in new playlist"), this, SLOT(OpenInNewPlaylist()));

    collection_menu_->addSeparator();
#ifdef HAVE_GSTREAMER
    organise_action_ = collection_menu_->addAction(IconLoader::Load("edit-copy"), tr("Copy to collection..."), this, SLOT(Organise()));
#endif
    delete_action_ = collection_menu_->addAction(IconLoader::Load("edit-delete"), tr("Delete from device..."), this, SLOT(Delete()));
  }

  menu_index_ = currentIndex();

  const QModelIndex device_index = MapToDevice(menu_index_);
  const QModelIndex collection_index = MapToCollection(menu_index_);

  if (device_index.isValid()) {
    const bool is_plugged_in = app_->device_manager()->GetLister(device_index.row());
    const bool is_remembered = app_->device_manager()->GetDatabaseId(device_index.row()) != -1;

    forget_action_->setEnabled(is_remembered);
    eject_action_->setEnabled(is_plugged_in);

    device_menu_->popup(e->globalPos());
  }
  else if (collection_index.isValid()) {
    const QModelIndex parent_device_index = FindParentDevice(menu_index_);

    bool is_filesystem_device = false;
    if (parent_device_index.isValid()) {
      std::shared_ptr<ConnectedDevice> device = app_->device_manager()->GetConnectedDevice(parent_device_index.row());
      qLog(Debug) << device->LocalPath();
      if (device && !device->LocalPath().isEmpty()) is_filesystem_device = true;
    }

    organise_action_->setEnabled(is_filesystem_device);

    collection_menu_->popup(e->globalPos());
  }

}

QModelIndex DeviceView::MapToDevice(const QModelIndex &merged_model_index) const {

  QModelIndex sort_model_index = merged_model_->mapToSource(merged_model_index);
  if (sort_model_index.model() != sort_model_) return QModelIndex();
  return sort_model_->mapToSource(sort_model_index);

}

QModelIndex DeviceView::FindParentDevice(const QModelIndex &merged_model_index) const {

  QModelIndex index = merged_model_->FindSourceParent(merged_model_index);
  if (index.model() != sort_model_) return QModelIndex();
  return sort_model_->mapToSource(index);

}

QModelIndex DeviceView::MapToCollection(const QModelIndex &merged_model_index) const {

  QModelIndex sort_model_index = merged_model_->mapToSource(merged_model_index);
  if (const QSortFilterProxyModel *sort_model = qobject_cast<const QSortFilterProxyModel*>(sort_model_index.model())) {
    return sort_model->mapToSource(sort_model_index);
  }
  return QModelIndex();

}

void DeviceView::Connect() {
  QModelIndex device_idx = MapToDevice(menu_index_);
  app_->device_manager()->data(device_idx, MusicStorage::Role_StorageForceConnect);
}

void DeviceView::DeviceConnected(int row) {

  std::shared_ptr<ConnectedDevice> device = app_->device_manager()->GetConnectedDevice(row);
  if (!device) return;

  QModelIndex sort_idx = sort_model_->mapFromSource(app_->device_manager()->index(row));

  QSortFilterProxyModel *sort_model = new QSortFilterProxyModel(device->model());
  sort_model->setSourceModel(device->model());
  sort_model->setSortRole(CollectionModel::Role_SortText);
  sort_model->setDynamicSortFilter(true);
  sort_model->sort(0);
  merged_model_->AddSubModel(sort_idx, sort_model);

  expand(menu_index_);

}

void DeviceView::DeviceDisconnected(int row) {
  merged_model_->RemoveSubModel(sort_model_->mapFromSource(app_->device_manager()->index(row)));
}

void DeviceView::Forget() {

  QModelIndex device_idx = MapToDevice(menu_index_);
  QString unique_id = app_->device_manager()->data(device_idx, DeviceManager::Role_UniqueId).toString();
  if (app_->device_manager()->GetLister(device_idx.row()) && app_->device_manager()->GetLister(device_idx.row())->AskForScan(unique_id)) {
    std::unique_ptr<QMessageBox> dialog(new QMessageBox(
        QMessageBox::Question, tr("Forget device"),
        tr("Forgetting a device will remove it from this list and Strawberry will have to rescan all the songs again next time you connect it."),
        QMessageBox::Cancel, this));
    QPushButton *forget = dialog->addButton(tr("Forget device"), QMessageBox::DestructiveRole);
    dialog->exec();

    if (dialog->clickedButton() != forget) return;
  }

  app_->device_manager()->Forget(device_idx.row());

}

void DeviceView::Properties() {
  properties_dialog_->ShowDevice(MapToDevice(menu_index_).row());
}

void DeviceView::mouseDoubleClickEvent(QMouseEvent *event) {

  AutoExpandingTreeView::mouseDoubleClickEvent(event);

  QModelIndex merged_index = indexAt(event->pos());
  QModelIndex device_index = MapToDevice(merged_index);
  if (device_index.isValid()) {
    if (!app_->device_manager()->GetConnectedDevice(device_index.row())) {
      menu_index_ = merged_index;
      Connect();
    }
  }

}

SongList DeviceView::GetSelectedSongs() const {

  QModelIndexList selected_merged_indexes = selectionModel()->selectedRows();
  SongList songs;
  for (const QModelIndex &merged_index : selected_merged_indexes) {
    QModelIndex collection_index = MapToCollection(merged_index);
    if (!collection_index.isValid()) continue;

    const CollectionModel *collection = qobject_cast<const CollectionModel*>(collection_index.model());
    if (!collection) continue;

    songs << collection->GetChildSongs(collection_index);
  }
  return songs;

}

void DeviceView::Load() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->clear_first_ = true;
  }
  emit AddToPlaylistSignal(data);
}

void DeviceView::AddToPlaylist() {
  emit AddToPlaylistSignal(model()->mimeData(selectedIndexes()));
}

void DeviceView::OpenInNewPlaylist() {
  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->open_in_new_playlist_ = true;
  }
  emit AddToPlaylistSignal(data);
}

#ifdef HAVE_GSTREAMER
void DeviceView::Delete() {

  if (selectedIndexes().isEmpty()) return;

  // Take the device of the first selected item
  QModelIndex device_index = FindParentDevice(selectedIndexes()[0]);
  if (!device_index.isValid()) return;

  if (QMessageBox::question(this, tr("Delete files"),
        tr("These files will be deleted from the device, are you sure you want to continue?"),
        QMessageBox::Yes, QMessageBox::Cancel) != QMessageBox::Yes)
    return;

  std::shared_ptr<MusicStorage> storage = device_index.data(MusicStorage::Role_Storage).value<std::shared_ptr<MusicStorage>>();

  DeleteFiles *delete_files = new DeleteFiles(app_->task_manager(), storage);
  connect(delete_files, SIGNAL(Finished(SongList)), SLOT(DeleteFinished(SongList)));
  delete_files->Start(GetSelectedSongs());

}

void DeviceView::Organise() {

  SongList songs = GetSelectedSongs();
  QStringList filenames;
  for (const Song &song : songs) {
    filenames << song.url().toLocalFile();
  }

  organise_dialog_->SetCopy(true);
  organise_dialog_->SetFilenames(filenames);
  organise_dialog_->show();

}
#endif

void DeviceView::Unmount() {
  QModelIndex device_idx = MapToDevice(menu_index_);
  app_->device_manager()->Unmount(device_idx.row());
}

#ifdef HAVE_GSTREAMER
void DeviceView::DeleteFinished(const SongList &songs_with_errors) {

  if (songs_with_errors.isEmpty()) return;

  OrganiseErrorDialog *dialog = new OrganiseErrorDialog(this);
  dialog->Show(OrganiseErrorDialog::Type_Delete, songs_with_errors);
  // It deletes itself when the user closes it

}
#endif

bool DeviceView::CanRecursivelyExpand(const QModelIndex &index) const {
  // Never expand devices
  return index.parent().isValid();
}
