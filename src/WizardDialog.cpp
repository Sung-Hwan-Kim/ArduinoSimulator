#include "WizardDialog.h"
#include "AppSettings.h"
#include "Catalog.h"
#include "WheelGuard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

WizardDialog::WizardDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("회로 마법사 - 센서 자동 배치·배선·코드 생성"));
    auto* v = new QVBoxLayout(this);
    auto* info = new QLabel(QStringLiteral("사용할 센서와 부품을 체크하고 수량·규격을 고르세요. <b>확인</b>을 누르면 보드 주변에 자동으로 배치하고, "
                                           "핀 할당(PWM/아날로그/I2C)·필요한 저항 삽입·배선·예제 스케치 생성까지 한 번에 진행합니다."));
    info->setWordWrap(true);
    v->addWidget(info);

    auto* h = new QHBoxLayout;
    h->addWidget(new QLabel(QStringLiteral("보드:")));
    m_board = new QComboBox;
    for (const PartDef& d : Catalog::instance().parts()) if (d.isBoard) m_board->addItem(d.name, d.id);
    m_board->setCurrentIndex(qMax(0, m_board->findData(AppSettings::instance().defaultBoard)));
    h->addWidget(m_board, 1);
    v->addLayout(h);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ QStringLiteral("부품 / 센서"), QStringLiteral("수량"), QStringLiteral("규격") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setColumnWidth(1, 72);
    m_tree->setColumnWidth(2, 260);
    v->addWidget(m_tree, 1);

    const Catalog& cat = Catalog::instance();
    for (const QString& c : cat.categories()) {
        QTreeWidgetItem* top = nullptr;
        for (const PartDef& d : cat.parts()) {
            if (d.category != c || d.isBoard) continue;
            bool autoable = false;
            for (const PinDef& p : d.pins)
                if (p.role != PinRole::Passive && p.role != PinRole::NC && p.role != PinRole::Board) autoable = true;
            if (!autoable) continue;
            if (!top) {
                top = new QTreeWidgetItem(m_tree, { c });
                QFont f = top->font(0); f.setBold(true); top->setFont(0, f);
                top->setFlags(Qt::ItemIsEnabled);
            }
            auto* it = new QTreeWidgetItem(top, { d.name });
            it->setData(0, Qt::UserRole, d.id);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            it->setCheckState(0, Qt::Unchecked);
            it->setToolTip(0, d.desc);
            auto* spin = new QSpinBox;
            spin->setRange(1, 8);
            guardWheel(spin, m_tree->viewport());
            m_tree->setItemWidget(it, 1, spin);
            const ParamDef* main = d.param("model");
            if (!main) main = d.param("color");
            if (!main) main = d.param("count");
            if (main && main->type == ParamType::Choice) {
                auto* combo = new QComboBox;
                for (const ParamOption& o : main->options) combo->addItem(o.label, o.value);
                combo->setCurrentIndex(qMax(0, combo->findData(main->def)));
                combo->setProperty("paramKey", main->key);
                guardWheel(combo, m_tree->viewport());
                m_tree->setItemWidget(it, 2, combo);
            }
        }
        if (top) top->setExpanded(true);
    }

    m_clear = new QCheckBox(QStringLiteral("기존 회로를 지우고 새로 만들기"));
    m_clear->setChecked(true);
    v->addWidget(m_clear);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("자동 배치·배선"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);

    auto s = AppSettings::open();
    QByteArray geo = s->value("WizardDialog/Geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(640, 640);
}

QVector<WizardItem> WizardDialog::items() const
{
    QVector<WizardItem> out;
    for (int t = 0; t < m_tree->topLevelItemCount(); ++t) {
        QTreeWidgetItem* top = m_tree->topLevelItem(t);
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem* it = top->child(k);
            if (it->checkState(0) != Qt::Checked) continue;
            WizardItem wi;
            wi.id = it->data(0, Qt::UserRole).toString();
            if (auto* sp = qobject_cast<QSpinBox*>(m_tree->itemWidget(it, 1))) wi.count = sp->value();
            if (auto* cb = qobject_cast<QComboBox*>(m_tree->itemWidget(it, 2))) {
                wi.paramKey = cb->property("paramKey").toString();
                wi.paramValue = cb->currentData();
            }
            out << wi;
        }
    }
    return out;
}

QString WizardDialog::boardId() const { return m_board->currentData().toString(); }
bool WizardDialog::clearExisting() const { return m_clear->isChecked(); }

void WizardDialog::done(int r)
{
    auto s = AppSettings::open();
    s->setValue("WizardDialog/Geometry", saveGeometry());
    QDialog::done(r);
}
