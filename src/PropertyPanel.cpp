#include "PropertyPanel.h"
#include "Catalog.h"
#include "Theme.h"
#include "CircuitScene.h"
#include "PartItem.h"
#include "ResistorAdvisor.h"
#include "Units.h"
#include "WheelGuard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

PropertyPanel::PropertyPanel(CircuitScene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    m_root = new QVBoxLayout(holder);
    m_root->setContentsMargins(8, 8, 8, 8);
    scroll->setWidget(holder);
    outer->addWidget(scroll);
    rebuild();
}

void PropertyPanel::setPart(PartItem* part)
{
    if (part == m_part) return;
    m_part = part;
    rebuild();
}

void PropertyPanel::setSimulating(bool on)
{
    m_simulating = on;
    rebuild();
}

void PropertyPanel::applyParam(const QString& key, const QVariant& v)
{
    if (!m_part || m_updating) return;
    QStringList changed = m_part->setParam(key, v);
    emit paramChanged(m_part, key);
    // 선택지에 따라 다른 값이 바뀐 경우 패널 다시 구성
    if (changed.size() > 1 || m_part->def()->id == "resistor" || m_part->def()->id == "led") {
        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
    }
}

void PropertyPanel::refreshValues()
{
    if (!m_part) return;
    m_updating = true;
    for (auto it = m_editors.begin(); it != m_editors.end(); ++it) {
        QVariant v = m_part->param(it.key());
        if (auto* cb = qobject_cast<QCheckBox*>(it.value())) cb->setChecked(v.toBool());
        else if (auto* sl = qobject_cast<QSlider*>(it.value())) {
            const ParamDef* pd = m_part->def()->param(it.key());
            if (pd) {
                double t = pd->logScale ? (std::log10(qMax(v.toDouble(), pd->minV)) - std::log10(pd->minV)) / (std::log10(pd->maxV) - std::log10(pd->minV))
                                        : (v.toDouble() - pd->minV) / (pd->maxV - pd->minV);
                sl->setValue((int)std::round(t * 1000));
            }
        } else if (auto* sp = qobject_cast<QDoubleSpinBox*>(it.value())) sp->setValue(v.toDouble());
    }
    m_updating = false;
}

void PropertyPanel::rebuild()
{
    m_editors.clear();
    if (m_content) { m_content->deleteLater(); m_content = nullptr; }
    m_content = new QWidget;
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(0, 0, 0, 0);
    m_root->addWidget(m_content);

    if (!m_part) {
        auto* l = new QLabel(QStringLiteral("부품을 선택하면 규격과 속성이 여기에 표시됩니다.\n\n"
                                            "• 왼쪽 목록에서 부품을 끌어다 놓거나 더블클릭하여 배치\n"
                                            "• 핀(동그라미)을 클릭 → 다른 핀 클릭으로 배선\n"
                                            "• R: 회전, Del: 삭제, Ctrl+휠: 확대/축소\n"
                                            "• 시뮬레이션 중 센서값은 여기서 실시간 조절"));
        l->setWordWrap(true);
        l->setStyleSheet(QString("color:%1").arg(Theme::hintText().name()));
        lay->addWidget(l);
        lay->addStretch();
        return;
    }
    const PartDef* d = m_part->def();

    // ---- 머리글 ----
    auto* title = new QLabel(QString("<b style='font-size:14px'>%1</b>").arg(d->name));
    title->setWordWrap(true);
    lay->addWidget(title);
    auto* desc = new QLabel(d->desc);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1").arg(Theme::hintText().name()));
    lay->addWidget(desc);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    auto* refEdit = new QLineEdit(m_part->ref());
    refEdit->setEnabled(!m_simulating);
    connect(refEdit, &QLineEdit::editingFinished, this, [this, refEdit] {
        if (m_part && !refEdit->text().trimmed().isEmpty()) { m_part->setRef(refEdit->text().trimmed()); m_scene->notifyModified(); }
    });
    form->addRow(QStringLiteral("참조 기호"), refEdit);

    for (const ParamDef& pd : d->params) {
        if (pd.hidden) continue;
        const QString key = pd.key;
        bool enabled = !m_simulating || pd.live;
        QString label = pd.label + (pd.live ? QStringLiteral(" ⚡") : QString());
        QVariant cur = m_part->param(key);

        if (pd.type == ParamType::Bool) {
            auto* cb = new QCheckBox;
            cb->setChecked(cur.toBool());
            cb->setEnabled(enabled);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { applyParam(key, on); });
            form->addRow(label, cb);
            m_editors[key] = cb;
        } else if (pd.type == ParamType::Choice) {
            auto* combo = new QComboBox;
            guardWheel(combo, m_scroll->viewport());
            int sel = -1;
            for (int i = 0; i < pd.options.size(); ++i) {
                combo->addItem(pd.options[i].label, pd.options[i].value);
                if (pd.options[i].value == cur) sel = i;
            }
            if (sel < 0) { combo->addItem(cur.toString(), cur); sel = combo->count() - 1; }
            combo->setCurrentIndex(sel);
            combo->setEnabled(enabled);
            combo->setMaxVisibleItems(20);
            connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, key, combo](int i) { applyParam(key, combo->itemData(i)); });
            form->addRow(label, combo);
            m_editors[key] = combo;
        } else if (pd.live) {
            // 슬라이더 + 스핀박스
            auto* w = new QWidget;
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            auto* sl = new QSlider(Qt::Horizontal);
            sl->setRange(0, 1000);
            guardWheel(sl, m_scroll->viewport());
            auto* sp = new QDoubleSpinBox;
            guardWheel(sp, m_scroll->viewport());
            sp->setRange(pd.minV, pd.maxV);
            sp->setDecimals(pd.maxV - pd.minV > 500 ? 0 : 1);
            sp->setSuffix(" " + pd.unit);
            sp->setValue(cur.toDouble());
            sp->setMinimumWidth(90);
            auto toSlider = [pd](double v) {
                double t = pd.logScale ? (std::log10(qMax(v, pd.minV)) - std::log10(pd.minV)) / (std::log10(pd.maxV) - std::log10(pd.minV))
                                       : (v - pd.minV) / (pd.maxV - pd.minV);
                return (int)std::round(qBound(0.0, t, 1.0) * 1000);
            };
            auto fromSlider = [pd](int s) {
                double t = s / 1000.0;
                return pd.logScale ? std::pow(10.0, std::log10(pd.minV) + t * (std::log10(pd.maxV) - std::log10(pd.minV)))
                                   : pd.minV + t * (pd.maxV - pd.minV);
            };
            sl->setValue(toSlider(cur.toDouble()));
            connect(sl, &QSlider::valueChanged, this, [this, key, sp, fromSlider](int s) {
                if (m_updating) return;
                double v = fromSlider(s);
                m_updating = true; sp->setValue(v); m_updating = false;
                if (m_part) { m_part->setParam(key, v); emit paramChanged(m_part, key); }
            });
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key, sl, toSlider](double v) {
                if (m_updating) return;
                m_updating = true; sl->setValue(toSlider(v)); m_updating = false;
                if (m_part) { m_part->setParam(key, v); emit paramChanged(m_part, key); }
            });
            h->addWidget(sl, 1);
            h->addWidget(sp);
            form->addRow(label, w);
            m_editors[key] = sl;
            m_editors[key + "#spin"] = sp;
        } else {
            // 숫자: 편집 가능한 콤보 (표준값 추천 목록)
            auto* combo = new QComboBox;
            guardWheel(combo, m_scroll->viewport());
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->setMaxVisibleItems(20);
            for (const ParamOption& o : pd.options) combo->addItem(o.label, o.value);
            QString curText = pd.unit.isEmpty() ? QString::number(cur.toDouble(), 'g', 6) : Units::format(cur.toDouble(), pd.unit, 4);
            if (pd.unit == "mA" || pd.unit == "V" || pd.unit == "K" || pd.unit == "A") curText = QString::number(cur.toDouble(), 'g', 6) + " " + pd.unit;
            combo->setEditText(curText);
            combo->setEnabled(enabled);
            combo->setToolTip(QStringLiteral("값을 직접 입력할 수 있습니다 (예: 4.7k, 2M2, 100n, 330)"));
            auto commit = [this, key, combo, pd]() {
                bool ok = false;
                QString t = combo->currentText();
                double v = Units::parse(t.remove(pd.unit).trimmed(), &ok);
                if (!ok) { emit message(QStringLiteral("숫자를 해석할 수 없습니다: %1").arg(combo->currentText())); return; }
                v = qBound(pd.minV, v, pd.maxV);
                if (m_part && std::fabs(m_part->paramD(key) - v) > 1e-15) applyParam(key, v);
            };
            connect(combo->lineEdit(), &QLineEdit::editingFinished, this, commit);
            connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, key, combo](int i) {
                QVariant v = combo->itemData(i);
                if (v.isValid()) applyParam(key, v.toDouble());
            });
            form->addRow(label + (pd.unit.isEmpty() ? QString() : QString(" (%1)").arg(pd.unit)), combo);
            m_editors[key] = combo;
        }
    }
    auto* box = new QGroupBox(QStringLiteral("규격 / 속성"));
    box->setLayout(form);
    lay->addWidget(box);

    // ---- 저항 추천 ----
    if (d->id == "resistor") {
        auto* g = new QGroupBox(QStringLiteral("저항값 자동 추천"));
        auto* v = new QVBoxLayout(g);
        ResistorAdvice a = ResistorAdvisor::adviseFor(m_scene, m_part);
        auto* l = new QLabel(a.ok ? QStringLiteral("<b>%1</b>: 추천 <b>%2 %3</b><br><small>%4</small>")
                                        .arg(a.purpose, Units::format(a.ohms, QStringLiteral("Ω")), Units::powerLabel(a.watts), a.reason.toHtmlEscaped().replace("\n", "<br>"))
                                  : QStringLiteral("<small>%1</small>").arg(a.reason));
        l->setWordWrap(true);
        l->setTextFormat(Qt::RichText);
        v->addWidget(l);
        auto* btn = new QPushButton(QStringLiteral("추천값 적용"));
        btn->setEnabled(a.ok && !m_simulating);
        connect(btn, &QPushButton::clicked, this, [this, a] {
            if (!m_part) return;
            m_part->setParam("R", a.ohms);
            if (m_part->paramD("P", 0.25) < a.watts) m_part->setParam("P", a.watts);
            emit paramChanged(m_part, "R");
            emit message(QStringLiteral("%1 ← %2 (%3)").arg(m_part->ref(), Units::format(a.ohms, QStringLiteral("Ω")), a.purpose));
            m_scene->notifyModified();
            rebuild();
        });
        v->addWidget(btn);
        lay->addWidget(g);
    }
    // ---- LED 저항 안내 ----
    QString hint = ResistorAdvisor::ledHint(m_scene, m_part);
    if (!hint.isEmpty()) {
        auto* g = new QGroupBox(QStringLiteral("직렬 저항 추천"));
        auto* v = new QVBoxLayout(g);
        auto* l = new QLabel(hint);
        l->setWordWrap(true);
        l->setTextFormat(Qt::RichText);
        v->addWidget(l);
        auto* btn = new QPushButton(QStringLiteral("저항 자동 삽입"));
        btn->setEnabled(!m_simulating);
        connect(btn, &QPushButton::clicked, this, [this] {
            if (!m_part) return;
            QStringList rep;
            int n = ResistorAdvisor::insertMissingLedResistors(m_scene, m_part, &rep);
            emit message(n > 0 ? rep.join("\n") : QStringLiteral("직렬 저항이 필요한 핀이 없습니다 (아두이노 핀/5V 에 직접 연결된 핀이 없음)."));
            rebuild();
        });
        v->addWidget(btn);
        lay->addWidget(g);
    } else {
        QString ph = ResistorAdvisor::partHint(m_part);
        if (!ph.isEmpty()) {
            auto* g = new QGroupBox(QStringLiteral("연결 / 저항 안내"));
            auto* v = new QVBoxLayout(g);
            auto* l = new QLabel(ph);
            l->setWordWrap(true);
            l->setTextFormat(Qt::RichText);
            v->addWidget(l);
            lay->addWidget(g);
        }
    }

    // ---- 핀 목록 ----
    if (!d->isBoard) {
        QStringList pins;
        for (const PinDef& p : d->pins) pins << p.name;
        auto* pl = new QLabel(QStringLiteral("<small>핀: %1</small>").arg(pins.join(" · ")));
        pl->setWordWrap(true);
        pl->setStyleSheet(QString("color:%1").arg(Theme::hintText().name()));
        lay->addWidget(pl);
    }
    if (m_simulating && !d->clickParam.isEmpty()) {
        auto* tip = new QLabel(QStringLiteral("💡 시뮬레이션 중 부품을 클릭하여 조작할 수 있습니다."));
        tip->setWordWrap(true);
        lay->addWidget(tip);
    }
    lay->addStretch();
}
