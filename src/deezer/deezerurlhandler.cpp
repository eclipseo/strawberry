/*
 * Strawberry Music Player
 * Copyright 2018, Jonas Kvinge <jonas@jkvinge.net>
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

#include <QObject>
#include <QString>
#include <QUrl>

#include "core/application.h"
#include "core/taskmanager.h"
#include "core/iconloader.h"
#include "core/logging.h"
#include "core/song.h"
#include "deezer/deezerservice.h"
#include "deezerurlhandler.h"

DeezerUrlHandler::DeezerUrlHandler(
    Application *app, DeezerService *service)
    : UrlHandler(service), app_(app), service_(service), task_id_(-1) {

  connect(service, SIGNAL(StreamURLReceived(QUrl, QUrl, Song::FileType)), this, SLOT(GetStreamURLFinished(QUrl, QUrl, Song::FileType)));

}

UrlHandler::LoadResult DeezerUrlHandler::StartLoading(const QUrl &url) {

  LoadResult ret(url);
  if (task_id_ != -1) return ret;
  last_original_url_ = url;
  task_id_ = app_->task_manager()->StartTask(QString("Loading %1 stream...").arg(url.scheme()));
  service_->GetStreamURL(url);
  ret.type_ = LoadResult::WillLoadAsynchronously;
  return ret;

}

void DeezerUrlHandler::GetStreamURLFinished(QUrl original_url, QUrl media_url, Song::FileType filetype) {

  if (task_id_ == -1) return;
  CancelTask();
  emit AsyncLoadComplete(LoadResult(original_url, LoadResult::TrackAvailable, media_url, filetype));

}

void DeezerUrlHandler::CancelTask() {

  app_->task_manager()->SetTaskFinished(task_id_);
  task_id_ = -1;

}
