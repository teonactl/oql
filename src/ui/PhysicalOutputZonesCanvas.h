#pragma once
#include <QWidget>
#include <QString>

class Workspace;

// Editor grafico di TUTTI gli schermi virtuali di UN output fisico insieme:
// mostra il rettangolo del canvas (nelle proporzioni reali dello schermo
// rilevato/configurato, non stirato) con il poligono di ciascuno VirtualScreen
// sovrapposto, colorato in modo diverso. Click su un poligono lo seleziona
// (anche dalla lista esterna, vedi setSelectedScreen) — solo il poligono
// selezionato mostra gli handle trascinabili, gli altri restano visibili ma
// non editabili direttamente, per non creare ambiguità quando si sovrappongono.
//
// Non possiede i dati: li rilegge sempre da Workspace tramite gli id (le
// QList<PhysicalOutput>/<VirtualScreen> possono riallocare altrove).
class PhysicalOutputZonesCanvas : public QWidget {
    Q_OBJECT
public:
    explicit PhysicalOutputZonesCanvas(QWidget *parent = nullptr);

    void setOutput(Workspace *workspace, const QString &physicalOutputId);
    void setSelectedScreen(const QString &screenId);
    QString selectedScreen() const { return m_selectedScreenId; }

public slots:
    void addVertex();
    void removeSelectedVertex();
    void resetPolygon();

signals:
    void maskEdited();
    // Emesso quando l'utente clicca direttamente su un poligono (o fuori da
    // tutti) sul canvas — il chiamante sincronizza la selezione nella lista.
    void selectionChangedByClick(QString screenId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Rettangolo in pixel del widget in cui si disegna il canvas [0,1]x[0,1]
    // normalizzato — nelle proporzioni reali dell'output (letterbox/pillarbox
    // dentro l'area disponibile, mai stirato), così dividerlo in zone dà
    // davvero l'idea di come apparirà sullo schermo vero.
    QRectF canvasPixelRect() const;
    QPointF toPixel(QPointF normalized, const QRectF &canvasRect) const;
    QPointF toNormalized(QPointF pixel, const QRectF &canvasRect) const;
    static QColor colorForScreen(const QString &id);

    Workspace *m_workspace = nullptr;
    QString    m_outputId;
    QString    m_selectedScreenId;

    int  m_selectedVertex = -1;
    bool m_dragging        = false;
    static constexpr double kHandleRadiusPx = 10.0;
};
