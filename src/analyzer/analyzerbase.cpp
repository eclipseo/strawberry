/***************************************************************************
                          viswidget.cpp  -  description
                             -------------------
    begin                : Die Jan 7 2003
    copyright            : (C) 2003 by Max Howell
    email                : markey@web.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "config.h"

#include <cmath>

#include <QWidget>
#include <QPainter>
#include <QPalette>
#include <QTimerEvent>
#include <QtEvents>

#include "analyzerbase.h"

#include "core/logging.h"
#include "engine/enginebase.h"

// INSTRUCTIONS Base2D
// 1. do anything that depends on height() in init(), Base2D will call it before you are shown
// 2. otherwise you can use the constructor to initialise things
// 3. reimplement analyze(), and paint to canvas(), Base2D will update the widget when you return control to it
// 4. if you want to manipulate the scope, reimplement transform()
// 5. for convenience <vector> <qpixmap.h> <qwdiget.h> are pre-included
// TODO make an INSTRUCTIONS file
// can't mod scope in analyze you have to use transform

// TODO for 2D use setErasePixmap Qt function insetead of m_background

// make the linker happy only for gcc < 4.0
#if !(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 0)) && \
    !defined(Q_OS_WIN32)
template class Analyzer::Base<QWidget>;
#endif

Analyzer::Base::Base(QWidget *parent, uint scopeSize)
    : QWidget(parent),
      timeout_(40),
      fht_(new FHT(scopeSize)),
      engine_(nullptr),
      lastscope_(512),
      current_chunk_(0),
      new_frame_(false),
      is_playing_(false) {}

void Analyzer::Base::hideEvent(QHideEvent*) { timer_.stop(); }

void Analyzer::Base::showEvent(QShowEvent*) { timer_.start(timeout(), this); }

void Analyzer::Base::transform(Scope& scope) {

  // This is a standard transformation that should give an FFT scope that has bands for pretty analyzers

  // NOTE: Resizing here is redundant as FHT routines only calculate FHT::size() values scope.resize( fht_->size() );

  float *front = static_cast<float*>(&scope.front());

  float *f = new float[fht_->size()];
  fht_->copy(&f[0], front);
  fht_->logSpectrum(front, &f[0]);
  fht_->scale(front, 1.0 / 20);

  scope.resize(fht_->size() / 2);  // second half of values are rubbish
  delete[] f;

}

void Analyzer::Base::paintEvent(QPaintEvent *e) {

  QPainter p(this);
  p.fillRect(e->rect(), palette().color(QPalette::Window));

  switch (engine_->state()) {
    case Engine::Playing: {
      const Engine::Scope& thescope = engine_->scope(timeout_);
      int i = 0;

      // convert to mono here - our built in analyzers need mono, but the engines provide interleaved pcm
      for (uint x = 0; (int)x < fht_->size(); ++x) {
        lastscope_[x] = double(thescope[i] + thescope[i + 1]) / (2 * (1 << 15));
        i += 2;
      }

      is_playing_ = true;
      transform(lastscope_);
      analyze(p, lastscope_, new_frame_);

      lastscope_.resize(fht_->size());

      break;
    }
    case Engine::Paused:
      is_playing_ = false;
      analyze(p, lastscope_, new_frame_);
      break;

    default:
      is_playing_ = false;
      demo(p);
  }

  new_frame_ = false;

}

int Analyzer::Base::resizeExponent(int exp) {

  if (exp < 3)
    exp = 3;
  else if (exp > 9)
    exp = 9;

  if (exp != fht_->sizeExp()) {
    delete fht_;
    fht_ = new FHT(exp);
  }
  return exp;

}

int Analyzer::Base::resizeForBands(int bands) {

  int exp;
  if (bands <= 8)
    exp = 4;
  else if (bands <= 16)
    exp = 5;
  else if (bands <= 32)
    exp = 6;
  else if (bands <= 64)
    exp = 7;
  else if (bands <= 128)
    exp = 8;
  else
    exp = 9;

  resizeExponent(exp);
  return fht_->size() / 2;

}

void Analyzer::Base::demo(QPainter& p) {

  static int t = 201;  // FIXME make static to namespace perhaps

  if (t > 999) t = 1;  // 0 = wasted calculations
  if (t < 201) {
    Scope s(32);

    const double dt = double(t) / 200;
    for (uint i = 0; i < s.size(); ++i)
      s[i] = dt * (sin(M_PI + (i * M_PI) / s.size()) + 1.0);

    analyze(p, s, new_frame_);
  }
  else
    analyze(p, Scope(32, 0), new_frame_);

  ++t;

}

void Analyzer::Base::polishEvent() {
  init();
}

void Analyzer::interpolate(const Scope& inVec, Scope& outVec) {

  double pos = 0.0;
  const double step = (double)inVec.size() / outVec.size();

  for (uint i = 0; i < outVec.size(); ++i, pos += step) {
    const double error = pos - std::floor(pos);
    const uint64_t offset = (uint64_t)pos;

    uint64_t indexLeft = offset + 0;

    if (indexLeft >= inVec.size()) indexLeft = inVec.size() - 1;

    uint64_t indexRight = offset + 1;

    if (indexRight >= inVec.size()) indexRight = inVec.size() - 1;

    outVec[i] = inVec[indexLeft] * (1.0 - error) + inVec[indexRight] * error;
  }

}

void Analyzer::initSin(Scope& v, const uint size) {

  double step = (M_PI * 2) / size;
  double radian = 0;

  for (uint i = 0; i < size; i++) {
    v.push_back(sin(radian));
    radian += step;
  }

}

void Analyzer::Base::timerEvent(QTimerEvent *e) {

  QWidget::timerEvent(e);
  if (e->timerId() != timer_.timerId()) return;

  new_frame_ = true;
  update();

}
