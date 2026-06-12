#include "AboutDialog.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Informazioni su OQL"));
    setFixedWidth(380);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(24, 24, 24, 20);

    // App name
    auto *nameLabel = new QLabel("OQL", this);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(22);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    nameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(nameLabel);

    // Subtitle
    auto *subLabel = new QLabel(tr("Show Control Software"), this);
    subLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(subLabel);

    layout->addSpacing(4);

    // Version + build
    const QString version = QApplication::applicationVersion();
    const QString build   = QString(__DATE__) + "  " + QString(__TIME__);
    auto *verLabel = new QLabel(
        QString("<b>Versione:</b> %1<br><b>Build:</b> %2").arg(version, build), this);
    verLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(verLabel);

    layout->addSpacing(4);

    // Links
    auto *linksLabel = new QLabel(
        R"(<a href="https://github.com/teonactl/oql">GitHub</a>)"
        "  &nbsp;·&nbsp;  "
        R"(<a href="https://github.com/teonactl/oql/wiki">Wiki / Documentazione</a>)",
        this);
    linksLabel->setAlignment(Qt::AlignCenter);
    linksLabel->setOpenExternalLinks(true);
    layout->addWidget(linksLabel);

    layout->addSpacing(8);

    // Close button
    auto *btnBox = new QHBoxLayout();
    btnBox->addStretch();
    auto *closeBtn = new QPushButton("Chiudi", this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnBox->addWidget(closeBtn);
    btnBox->addStretch();
    layout->addLayout(btnBox);
}
