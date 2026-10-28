#include "ProjectionCalibrationToolbar.h"
#include <QPushButton>
#include <QHBoxLayout>

ProjectionCalibrationToolbar::ProjectionCalibrationToolbar(QWidget *parent)
    // Qt::Tool: finestra flottante indipendente (niente icona separata in
    // taskbar, resta sopra la finestra principale) invece di un widget
    // figlio sovrapposto al contenuto da calibrare.
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Calibrazione — strumenti"));
    setStyleSheet("background: #2a2a2a;");

    auto makeBtn = [this](const QString &text, const QString &tooltip) {
        auto *b = new QPushButton(text, this);
        b->setMinimumHeight(30);
        b->setStyleSheet(
            "QPushButton { background: #3a3a3a; color: #ffffff; "
            "border: 1px solid #888; border-radius: 4px; padding: 4px 10px; "
            "font-weight: bold; font-size: 12px; } "
            "QPushButton:hover { background: #4a4a4a; }");
        b->setToolTip(tooltip);
        return b;
    };

    m_addColBtn    = makeBtn(tr("Col +"),  tr("Aggiungi colonna alla griglia"));
    m_removeColBtn = makeBtn(tr("Col −"),  tr("Rimuovi colonna dalla griglia"));
    m_addRowBtn    = makeBtn(tr("Riga +"), tr("Aggiungi riga alla griglia"));
    m_removeRowBtn = makeBtn(tr("Riga −"), tr("Rimuovi riga dalla griglia"));
    m_resetBtn     = makeBtn(tr("Reset"),  tr("Reset calibrazione"));
    m_loadImageBtn = makeBtn(tr("Carica immagine…"),
                              tr("Usa un'immagine a scelta come riferimento invece della griglia"));
    m_useBuiltinBtn = makeBtn(tr("Pattern griglia"),
                               tr("Torna al pattern di test incorporato"));
    m_useBuiltinBtn->hide();
    m_closeBtn = makeBtn(tr("✕ Chiudi"), tr("Chiudi la calibrazione"));
    m_closeBtn->setStyleSheet(
        "QPushButton { background: #6a2a2a; color: #ffffff; "
        "border: 1px solid #a88; border-radius: 4px; padding: 4px 10px; "
        "font-weight: bold; font-size: 12px; } "
        "QPushButton:hover { background: #8a3a3a; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    for (QPushButton *b : {m_addColBtn, m_removeColBtn, m_addRowBtn, m_removeRowBtn,
                           m_resetBtn, m_loadImageBtn, m_useBuiltinBtn, m_closeBtn})
        layout->addWidget(b);
}
