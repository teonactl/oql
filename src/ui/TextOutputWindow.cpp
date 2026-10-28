#include "TextOutputWindow.h"
#include "engine/TextCue.h"
#include <QLabel>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFont>

TextOutputWindow::TextOutputWindow(QWidget *parent, bool embedded)
    : QWidget(parent, embedded ? Qt::Widget : (Qt::Window | Qt::FramelessWindowHint))
{
    if (!embedded) {
        setWindowTitle("Text Output — OQL");
        setStyleSheet("background: black;");
        resize(1280, 720);
    } else {
        // Widget figlio, non finestra top-level: basta non riempire lo
        // sfondo (nessuno stylesheet opaco) perché il contenuto del
        // genitore resti visibile sotto — niente WA_TranslucentBackground,
        // che serve solo per la vera trasparenza compositata a livello di
        // finestra top-level.
        setStyleSheet("background: transparent;");
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet("background: transparent; color: white;");
    layout->addWidget(m_label);

    m_opacity = new QGraphicsOpacityEffect(m_label);
    m_label->setGraphicsEffect(m_opacity);
}

void TextOutputWindow::showCue(const TextCue *cue) {
    if (m_connection) disconnect(m_connection);
    if (!cue) { clearText(); return; }

    QFont font(cue->fontFamily(), cue->fontSize());
    font.setBold(cue->bold());
    font.setItalic(cue->italic());
    m_label->setFont(font);

    const QString textHex = cue->textColor().name(QColor::HexArgb);
    const QString bgHex   = cue->backgroundColor().name(QColor::HexArgb);
    setStyleSheet(QString("background: %1;").arg(bgHex));
    m_label->setStyleSheet(QString("background: transparent; color: %1;").arg(textHex));

    m_label->setAlignment(Qt::Alignment(cue->alignment()));
    m_label->setText(cue->text());

    m_opacity->setOpacity(cue->visualLevel());
    m_connection = connect(cue, &Cue::displayChanged, this, [this, cue]() {
        m_opacity->setOpacity(cue->visualLevel());
    });

    show();
    raise();
}

void TextOutputWindow::clearText() {
    if (m_connection) disconnect(m_connection);
    m_label->clear();
    m_opacity->setOpacity(1.0);
}

void TextOutputWindow::toggleFullScreen() {
    if (isFullScreen()) showNormal();
    else                showFullScreen();
}

void TextOutputWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void TextOutputWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || window()->isFullScreen()) return;
    m_draggingWindow     = true;
    m_dragStartGlobalPos = event->globalPosition().toPoint();
    m_dragStartWindowPos = window()->pos();
}

void TextOutputWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_draggingWindow) return;
    const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobalPos;
    window()->move(m_dragStartWindowPos + delta);
}

void TextOutputWindow::mouseReleaseEvent(QMouseEvent *) {
    m_draggingWindow = false;
}

void TextOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
