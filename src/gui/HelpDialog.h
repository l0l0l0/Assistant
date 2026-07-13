#pragma once

#include <QDialog>
#include <QVector>

class QTabWidget;
class QLineEdit;
class QLabel;
class QTextBrowser;

namespace ibom::gui {

/**
 * @brief Exhaustive in-app reference, callable from anywhere (F1 / Help menu).
 *
 * Ten tabs covering every feature of the application, plus a full-text search
 * across all tabs (Ctrl+F inside the dialog): matches are highlighted in
 * every page, Enter/Shift+Enter cycle through them across tabs.
 */
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);

    /// Tab indices — the single source of truth for MainWindow's Help-menu
    /// wiring (raw ints there went stale every time a tab was inserted).
    enum Tab {
        GettingStarted = 0,
        Shortcuts,
        Calibration,
        Alignment,
        LensAdapters,
        Inspection,
        InspectionTools,
        Overlay,
        Export,
        Troubleshooting
    };

    /// Open the dialog on a specific tab by index.
    void showTab(int index);

private:
    void createTabs();
    void addPage(const QString& title, const QString& html);

    // ── Full-text search across all tabs ────────────────────────
    void runSearch(const QString& query);
    void findNext(bool backwards);
    /// Highlight every occurrence in one page; returns the match count.
    int  highlightAll(QTextBrowser* browser, const QString& query);

    QTabWidget*             m_tabs       = nullptr;
    QLineEdit*              m_search     = nullptr;
    QLabel*                 m_searchInfo = nullptr;
    QVector<QTextBrowser*>  m_pages;
    QVector<int>            m_matchCounts;   // per page, for the current query
};

} // namespace ibom::gui
