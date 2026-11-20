#include "SettingsDialog.h"
#include "AppSettings.h"
#include "Catalog.h"
#include "Units.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("환경설정"));
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    root->addWidget(tabs);

    // ---------------- 일반 ----------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        auto* row = new QHBoxLayout;
        m_workDir = new QLineEdit;
        auto* browse = new QPushButton(QStringLiteral("찾아보기..."));
        auto* open = new QPushButton(QStringLiteral("열기"));
        row->addWidget(m_workDir, 1);
        row->addWidget(browse);
        row->addWidget(open);
        f->addRow(QStringLiteral("작업 폴더"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("작업 폴더 선택"), m_workDir->text());
            if (!d.isEmpty()) m_workDir->setText(QDir::toNativeSeparators(d));
        });
        connect(open, &QPushButton::clicked, this, [this] {
            QString d = m_workDir->text().isEmpty() ? AppSettings::instance().effectiveWorkDir() : m_workDir->text();
            QDir().mkpath(d);
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        m_board = new QComboBox;
        for (const PartDef& d : Catalog::instance().parts()) if (d.isBoard) m_board->addItem(d.name, d.id);
        f->addRow(QStringLiteral("새 회로 기본 보드"), m_board);
        m_openLast = new QCheckBox(QStringLiteral("시작할 때 마지막으로 연 파일 열기"));
        f->addRow(QString(), m_openLast);
        m_autoSave = new QSpinBox;
        m_autoSave->setRange(0, 120);
        m_autoSave->setSuffix(QStringLiteral(" 분 (0 = 사용 안 함)"));
        f->addRow(QStringLiteral("자동 저장 간격"), m_autoSave);
        auto* ini = new QLabel(QStringLiteral("<small>설정 파일: %1<br>창 위치·크기·도크 배치도 이 파일에 저장되어 다음 실행 때 복원됩니다.</small>")
                                   .arg(QDir::toNativeSeparators(AppSettings::iniPath())));
        ini->setWordWrap(true);
        ini->setTextInteractionFlags(Qt::TextSelectableByMouse);
        f->addRow(ini);
        tabs->addTab(w, QStringLiteral("일반"));
    }
    // ---------------- 시뮬레이션 ----------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        m_pinLimit = new QDoubleSpinBox; m_pinLimit->setRange(5, 200); m_pinLimit->setSuffix(" mA");
        f->addRow(QStringLiteral("핀 최대 전류 (경고 기준)"), m_pinLimit);
        m_burn = new QDoubleSpinBox; m_burn->setRange(1.1, 10); m_burn->setSingleStep(0.1); m_burn->setSuffix(QStringLiteral(" 배"));
        f->addRow(QStringLiteral("LED 소손 기준 (정격 전류의)"), m_burn);
        m_usb = new QDoubleSpinBox; m_usb->setRange(100, 5000); m_usb->setSuffix(" mA");
        f->addRow(QStringLiteral("USB 5V 공급 한계"), m_usb);
        m_floating = new QCheckBox(QStringLiteral("플로팅(연결 안 된) 입력 경고"));
        f->addRow(QString(), m_floating);
        tabs->addTab(w, QStringLiteral("시뮬레이션"));
    }
    // ---------------- 저항 / 자동 배선 ----------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        m_ledTarget = new QDoubleSpinBox; m_ledTarget->setRange(1, 50); m_ledTarget->setSuffix(" mA");
        f->addRow(QStringLiteral("LED 목표 전류"), m_ledTarget);
        m_series = new QComboBox;
        m_series->addItem(QStringLiteral("E12 (10% 계열, 구하기 쉬움)"), "E12");
        m_series->addItem(QStringLiteral("E24 (5% 계열, 더 세밀함)"), "E24");
        f->addRow(QStringLiteral("추천 저항 표준 계열"), m_series);
        m_pull = new QComboBox;
        m_pull->setEditable(true);
        for (double v : { 1e3, 2.2e3, 4.7e3, 10e3, 22e3, 47e3, 100e3 }) m_pull->addItem(Units::format(v, QStringLiteral("Ω")), v);
        f->addRow(QStringLiteral("풀업/풀다운 저항"), m_pull);
        m_autoRes = new QCheckBox(QStringLiteral("자동 배선 시 LED 직렬 저항·분압 저항 자동 삽입"));
        f->addRow(QString(), m_autoRes);
        m_autoCode = new QCheckBox(QStringLiteral("자동 배선 후 스케치 코드 자동 생성"));
        f->addRow(QString(), m_autoCode);
        auto* note = new QLabel(QStringLiteral("<small>LED 저항 = (공급전압 - Vf) ÷ min(목표 전류, 정격×75%) → 위쪽 표준값으로 올림<br>"
                                               "LDR 분압 = √(R@10lux × R@1000lux), 서미스터 분압 = R25</small>"));
        note->setWordWrap(true);
        f->addRow(note);
        tabs->addTab(w, QStringLiteral("저항 추천·자동 배선"));
    }
    // ---------------- 편집기 ----------------
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        m_theme = new QComboBox;
        m_theme->addItem(QStringLiteral("다크 모드 (기본)"), "dark");
        m_theme->addItem(QStringLiteral("라이트 모드"), "light");
        f->addRow(QStringLiteral("스킨 모드"), m_theme);
        m_font = new QFontComboBox;
        m_font->setFontFilters(QFontComboBox::MonospacedFonts);
        f->addRow(QStringLiteral("코드 글꼴"), m_font);
        m_fontSize = new QSpinBox; m_fontSize->setRange(7, 32);
        f->addRow(QStringLiteral("글꼴 크기"), m_fontSize);
        m_grid = new QCheckBox(QStringLiteral("회로 격자 표시"));
        f->addRow(QString(), m_grid);
        m_snap = new QCheckBox(QStringLiteral("격자에 맞춤 (10px)"));
        f->addRow(QString(), m_snap);
        tabs->addTab(w, QStringLiteral("화면·편집기"));
    }
    // ---------------- 사용자 부품 ----------------
    {
        auto* w = new QWidget;
        auto* v = new QVBoxLayout(w);
        auto* info = new QLabel(QStringLiteral("카탈로그에 없는 센서/모듈을 추가할 수 있습니다. 핀 이름은 쉼표로 구분하세요.<br>"
                                               "<small>VCC/VIN/5V → 전원, GND → 접지, SDA/SCL → I2C, 그 외 첫 번째 핀 → 신호 출력. "
                                               "추가한 부품은 부품 목록의 <b>'사용자 부품'</b> 분류에 나타나며 자동 배선·코드 생성도 지원됩니다.</small>"));
        info->setWordWrap(true);
        v->addWidget(info);
        m_custom = new QTableWidget(0, 6);
        m_custom->setHorizontalHeaderLabels({ QStringLiteral("이름"), QStringLiteral("핀 목록"), QStringLiteral("출력 형식"),
                                              QStringLiteral("최소 V"), QStringLiteral("최대 V"), QStringLiteral("설명") });
        m_custom->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
        m_custom->setColumnWidth(0, 150);
        m_custom->setColumnWidth(1, 130);
        m_custom->setColumnWidth(2, 130);
        m_custom->setColumnWidth(3, 60);
        m_custom->setColumnWidth(4, 60);
        v->addWidget(m_custom, 1);
        auto* h = new QHBoxLayout;
        auto* add = new QPushButton(QStringLiteral("센서 추가"));
        auto* dup = new QPushButton(QStringLiteral("복제"));
        auto* del = new QPushButton(QStringLiteral("삭제"));
        h->addWidget(add); h->addWidget(dup); h->addWidget(del); h->addStretch();
        v->addLayout(h);
        connect(add, &QPushButton::clicked, this, [this] {
            addCustomRow(QStringLiteral("새 센서 %1").arg(m_custom->rowCount() + 1), "VCC,GND,OUT", "analog", 0, 5, QString());
            m_custom->editItem(m_custom->item(m_custom->rowCount() - 1, 0));
        });
        connect(dup, &QPushButton::clicked, this, [this] {
            int r = m_custom->currentRow();
            if (r < 0) return;
            auto* combo = qobject_cast<QComboBox*>(m_custom->cellWidget(r, 2));
            addCustomRow(m_custom->item(r, 0)->text() + QStringLiteral(" 복사"), m_custom->item(r, 1)->text(),
                         combo ? combo->currentData().toString() : "analog", m_custom->item(r, 3)->text().toDouble(),
                         m_custom->item(r, 4)->text().toDouble(), m_custom->item(r, 5)->text());
        });
        connect(del, &QPushButton::clicked, this, [this] {
            int r = m_custom->currentRow();
            if (r >= 0) m_custom->removeRow(r);
        });
        tabs->addTab(w, QStringLiteral("사용자 부품 (센서 추가)"));
    }

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("확인"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(bb);

    load();

    // 창 위치/크기 복원
    auto s = AppSettings::open();
    QByteArray geo = s->value("SettingsDialog/Geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(760, 520);
}

void SettingsDialog::addCustomRow(const QString& name, const QString& pins, const QString& output, double vmin, double vmax, const QString& desc)
{
    int r = m_custom->rowCount();
    m_custom->insertRow(r);
    m_custom->setItem(r, 0, new QTableWidgetItem(name));
    m_custom->setItem(r, 1, new QTableWidgetItem(pins));
    auto* combo = new QComboBox;
    combo->addItem(QStringLiteral("아날로그 출력"), "analog");
    combo->addItem(QStringLiteral("디지털 (감지=HIGH)"), "digital");
    combo->addItem(QStringLiteral("디지털 (감지=LOW)"), "digital_low");
    combo->setCurrentIndex(qMax(0, combo->findData(output)));
    m_custom->setCellWidget(r, 2, combo);
    m_custom->setItem(r, 3, new QTableWidgetItem(QString::number(vmin)));
    m_custom->setItem(r, 4, new QTableWidgetItem(QString::number(vmax)));
    m_custom->setItem(r, 5, new QTableWidgetItem(desc));
}

void SettingsDialog::load()
{
    const AppSettings& s = AppSettings::instance();
    m_workDir->setText(QDir::toNativeSeparators(s.workDir.isEmpty() ? s.effectiveWorkDir() : s.workDir));
    m_board->setCurrentIndex(qMax(0, m_board->findData(s.defaultBoard)));
    m_openLast->setChecked(s.openLastFile);
    m_autoSave->setValue(s.autoSaveMinutes);
    m_pinLimit->setValue(s.pinCurrentLimit);
    m_burn->setValue(s.ledBurnFactor);
    m_usb->setValue(s.usbCurrentLimit);
    m_floating->setChecked(s.warnFloating);
    m_ledTarget->setValue(s.ledTargetCurrent);
    m_series->setCurrentIndex(qMax(0, m_series->findData(s.resistorSeries)));
    m_pull->setEditText(Units::format(s.pullResistor, QStringLiteral("Ω")));
    m_autoRes->setChecked(s.autoInsertResistors);
    m_autoCode->setChecked(s.generateCodeOnAutoWire);
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(s.theme)));
    m_font->setCurrentFont(QFont(s.editorFont));
    m_fontSize->setValue(s.editorFontSize);
    m_grid->setChecked(s.showGrid);
    m_snap->setChecked(s.snapToGrid);
    for (const CustomPartSpec& c : s.customParts) addCustomRow(c.name, c.pins, c.output, c.vmin, c.vmax, c.desc);
}

void SettingsDialog::accept()
{
    AppSettings& s = AppSettings::instance();
    QString wd = QDir::fromNativeSeparators(m_workDir->text().trimmed());
    if (!wd.isEmpty() && !QDir(wd).exists()) {
        if (QMessageBox::question(this, QStringLiteral("작업 폴더"), QStringLiteral("폴더가 없습니다. 새로 만들까요?\n%1").arg(m_workDir->text())) == QMessageBox::Yes)
            QDir().mkpath(wd);
        else return;
    }
    s.workDir = wd;
    s.defaultBoard = m_board->currentData().toString();
    s.openLastFile = m_openLast->isChecked();
    s.autoSaveMinutes = m_autoSave->value();
    s.pinCurrentLimit = m_pinLimit->value();
    s.ledBurnFactor = m_burn->value();
    s.usbCurrentLimit = m_usb->value();
    s.warnFloating = m_floating->isChecked();
    s.ledTargetCurrent = m_ledTarget->value();
    s.resistorSeries = m_series->currentData().toString();
    bool ok = false;
    double pr = Units::parse(m_pull->currentText(), &ok);
    if (ok && pr > 0) s.pullResistor = pr;
    s.autoInsertResistors = m_autoRes->isChecked();
    s.generateCodeOnAutoWire = m_autoCode->isChecked();
    s.theme = m_theme->currentData().toString();
    s.editorFont = m_font->currentFont().family();
    s.editorFontSize = m_fontSize->value();
    s.showGrid = m_grid->isChecked();
    s.snapToGrid = m_snap->isChecked();
    s.customParts.clear();
    for (int r = 0; r < m_custom->rowCount(); ++r) {
        CustomPartSpec c;
        c.name = m_custom->item(r, 0) ? m_custom->item(r, 0)->text().trimmed() : QString();
        if (c.name.isEmpty()) continue;
        c.pins = m_custom->item(r, 1) ? m_custom->item(r, 1)->text() : "VCC,GND,OUT";
        auto* combo = qobject_cast<QComboBox*>(m_custom->cellWidget(r, 2));
        c.output = combo ? combo->currentData().toString() : "analog";
        c.vmin = m_custom->item(r, 3) ? m_custom->item(r, 3)->text().toDouble() : 0.0;
        c.vmax = m_custom->item(r, 4) ? m_custom->item(r, 4)->text().toDouble() : 5.0;
        c.desc = m_custom->item(r, 5) ? m_custom->item(r, 5)->text() : QString();
        s.customParts << c;
    }
    s.save();
    Catalog::setCustomParts(s.customParts);
    QDialog::accept();
}

void SettingsDialog::done(int r)
{
    auto s = AppSettings::open();
    s->setValue("SettingsDialog/Geometry", saveGeometry());
    QDialog::done(r);
}
