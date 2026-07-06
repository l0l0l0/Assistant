#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QButtonGroup;

namespace ibom::gui {

/**
 * Persistent, NON-MODAL control panel for multi-component alignment.
 *
 * Replaces the old per-component modal QMessageBox, which blocked the user
 * from selecting another component (or using the PCB Map) until dismissed.
 * This stays open for the whole collection step so the user can, at any time:
 *   - pick / re-pick a component in the BOM panel OR the PCB Map,
 *   - choose how to mark THIS component (opposite pads / pin 1 / body corners)
 *     — the method is per-component, switchable freely,
 *   - see how many landmarks are marked and finish when ready.
 *
 * It only drives the UI; the actual alignment math lives in Application, which
 * reacts to the signals below and pushes state back via the setters.
 */
class MultiAlignDialog : public QDialog {
    Q_OBJECT

public:
    enum Method { BodyCorners = 0, Pin1 = 1, OppositePads = 2 };

    explicit MultiAlignDialog(QWidget* parent = nullptr);

    /// Reflect the marking method currently in effect (checks the matching button).
    void setMethod(int method);
    /// Name of the component currently selected/being marked (empty = none yet).
    void setSelectedComponent(const QString& ref);
    /// Free-text instruction / progress line.
    void setStatus(const QString& text);
    /// Number of landmarks marked so far (enables Finish at >= 2).
    void setMarkedCount(int n);

signals:
    /// User picked a marking method (a Method value). Applies to the current
    /// (and subsequent) component(s).
    void methodChanged(int method);
    /// User wants to finish collecting and compute the alignment.
    void finishRequested();
    /// User cancelled the whole multi-align flow.
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QLabel*       m_compLabel    = nullptr;
    QLabel*       m_statusLabel  = nullptr;
    QLabel*       m_countLabel   = nullptr;
    QButtonGroup* m_methodGroup  = nullptr;
    QPushButton*  m_finishBtn    = nullptr;
};

} // namespace ibom::gui
