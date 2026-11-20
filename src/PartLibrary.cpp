#include "PartLibrary.h"
#include "Catalog.h"
#include "Theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QVBoxLayout>

static const char* kPartMime = "application/x-arduinosim-part";

PartTree::PartTree(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIndentation(14);
    setAnimated(true);
}

QStringList PartTree::mimeTypes() const
{
    return { QString::fromLatin1(kPartMime) };
}

QMimeData* PartTree::mimeData(const QList<QTreeWidgetItem*>& items) const
{
    if (items.isEmpty()) return nullptr;
    QString id = items.first()->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return nullptr;
    auto* m = new QMimeData;
    m->setData(kPartMime, id.toUtf8());
    return m;
}

PartLibrary::PartLibrary(QWidget* parent)
    : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("부품 검색 (예: LED, DHT22, 초음파, GL5528)"));
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);
    m_tree = new PartTree;
    v->addWidget(m_tree, 1);
    auto* hint = new QLabel(QStringLiteral("<small>더블클릭 또는 회로로 끌어다 놓기 · 규격은 배치 후 속성 창에서 선택</small>"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1").arg(Theme::hintText().name()));
    v->addWidget(hint);
    connect(m_search, &QLineEdit::textChanged, this, &PartLibrary::applyFilter);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) emit partActivated(id);
    });
    reload();
}

void PartLibrary::reload()
{
    for (QLabel* l : findChildren<QLabel*>()) l->setStyleSheet(QString("color:%1").arg(Theme::hintText().name()));
    m_tree->clear();
    const Catalog& cat = Catalog::instance();
    for (const QString& c : cat.categories()) {
        auto* top = new QTreeWidgetItem(m_tree, { c });
        QFont f = top->font(0); f.setBold(true); top->setFont(0, f);
        top->setFlags(Qt::ItemIsEnabled);
        int count = 0;
        for (const PartDef& d : cat.parts()) {
            if (d.category != c) continue;
            auto* it = new QTreeWidgetItem(top, { d.name });
            it->setData(0, Qt::UserRole, d.id);
            // 검색용: 이름 + 설명 + 모델명
            QString keys = d.name + " " + d.desc + " " + d.id;
            for (const ParamDef& p : d.params) for (const ParamOption& o : p.options) keys += " " + o.label;
            it->setData(0, Qt::UserRole + 1, keys);
            QString tip = QString("<b>%1</b><br>%2").arg(d.name, d.desc);
            const ParamDef* model = d.param("model");
            if (!model) model = d.param("color");
            if (model) {
                QStringList ms;
                for (const ParamOption& o : model->options) ms << "• " + o.label;
                tip += QStringLiteral("<br><br><b>선택 가능한 규격 (%1종)</b><br>").arg(ms.size()) + ms.join("<br>");
            } else if (d.id == "resistor") {
                tip += QStringLiteral("<br><br><b>E24 표준값 169종 (1Ω~10MΩ)</b> + 직접 입력, 1/8W~5W");
            }
            it->setToolTip(0, tip);
            ++count;
        }
        top->setText(0, QString("%1  (%2)").arg(c).arg(count));
        top->setExpanded(true);
    }
    applyFilter(m_search->text());
}

void PartLibrary::applyFilter(const QString& text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = m_tree->topLevelItem(i);
        int visible = 0;
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem* it = top->child(k);
            bool show = text.isEmpty() || it->data(0, Qt::UserRole + 1).toString().contains(text, Qt::CaseInsensitive);
            it->setHidden(!show);
            if (show) ++visible;
        }
        top->setHidden(visible == 0);
        if (!text.isEmpty()) top->setExpanded(true);
    }
}
