#include "MultiAlignDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QCloseEvent>

namespace ibom::gui {

MultiAlignDialog::MultiAlignDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Multi-Component Alignment"));
    // Non-modal + tool window that stays above the main window, so the user
    // can keep clicking in the camera view / BOM panel / PCB Map while it's up.
    setModal(false);
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Mark a few components to align the overlay. Pick each one in the "
           "BOM panel <b>or</b> the PCB Map, choose how to mark it, then click "
           "the green target(s) in the camera image."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_compLabel = new QLabel(this);
    m_compLabel->setWordWrap(true);
    layout->addWidget(m_compLabel);

    // Per-component marking method — switchable at any time.
    auto* methodBox = new QGroupBox(tr("Mark this component by:"), this);
    auto* mLayout = new QVBoxLayout(methodBox);
    m_methodGroup = new QButtonGroup(this);

    struct Opt { Method m; const char* label; const char* desc; };
    const Opt opts[] = {
        {OppositePads, QT_TR_NOOP("Opposite pads (most precise)"),
         QT_TR_NOOP("Click the 2 farthest-apart pads. Ideal for resistors / capacitors.")},
        {Pin1, QT_TR_NOOP("Pin 1"),
         QT_TR_NOOP("Single click on pin 1. Precise for ICs / connectors.")},
        {BodyCorners, QT_TR_NOOP("Body corners"),
         QT_TR_NOOP("Click 2 opposite corners of the body (eyeballed).")},
    };
    for (const auto& o : opts) {
        auto* rb = new QRadioButton(tr(o.label), methodBox);
        rb->setToolTip(tr(o.desc));
        m_methodGroup->addButton(rb, static_cast<int>(o.m));
        mLayout->addWidget(rb);
    }
    if (auto* def = m_methodGroup->button(OppositePads)) def->setChecked(true);
    layout->addWidget(methodBox);

    connect(m_methodGroup, &QButtonGroup::idClicked,
            this, [this](int id) { emit methodChanged(id); });

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color:#8892b8; font-size:11px;");
    layout->addWidget(m_statusLabel);

    m_countLabel = new QLabel(this);
    m_countLabel->setWordWrap(true);
    layout->addWidget(m_countLabel);
    setMarkedCount(0);

    auto* btnRow = new QHBoxLayout();
    m_finishBtn = new QPushButton(tr("Finish && Align"), this);
    m_finishBtn->setEnabled(false);
    connect(m_finishBtn, &QPushButton::clicked, this, [this]() { emit finishRequested(); });
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { emit cancelRequested(); });
    btnRow->addWidget(m_finishBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);

    setSelectedComponent(QString());
}

void MultiAlignDialog::setMethod(int method)
{
    if (auto* b = m_methodGroup->button(method)) b->setChecked(true);
}

void MultiAlignDialog::setSelectedComponent(const QString& ref)
{
    if (ref.isEmpty()) {
        m_compLabel->setText(tr("<b>No component selected</b> — pick one in the "
                                "BOM panel or click it on the PCB Map."));
    } else {
        m_compLabel->setText(tr("Marking: <b>%1</b>").arg(ref));
    }
}

void MultiAlignDialog::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}

void MultiAlignDialog::setMarkedCount(int n)
{
    m_countLabel->setText(tr("Marked: <b>%1</b> %2")
        .arg(n)
        .arg(n >= 4 ? tr("(≥4 — perspective corrected)")
           : n >= 2 ? tr("(≥2 — OK, 4 is better)")
                    : tr("(need ≥2)")));
    if (m_finishBtn) m_finishBtn->setEnabled(n >= 2);
}

void MultiAlignDialog::closeEvent(QCloseEvent* e)
{
    // The window's [x] is a cancel.
    emit cancelRequested();
    e->accept();
}

} // namespace ibom::gui
