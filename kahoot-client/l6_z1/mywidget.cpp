#include "mywidget.h"
#include "ui_mywidget.h"

#include <QMessageBox>

MyWidget::MyWidget(QWidget *parent) : QWidget(parent), ui(new Ui::MyWidget) {
    ui->setupUi(this);

    connect(ui->conectBtn, &QPushButton::clicked, this, &MyWidget::connectBtnHit);
    connect(ui->hostLineEdit, &QLineEdit::returnPressed, ui->conectBtn, &QPushButton::click);

    connect(ui->sendBtn, &QPushButton::clicked, this, &MyWidget::sendBtnHit);
    connect(ui->msgLineEdit, &QLineEdit::returnPressed, ui->sendBtn, &QPushButton::click);

    connect(ui->pushButtonMM, &QPushButton::clicked, this, &MyWidget::sendBtnHitMM);
    connect(ui->lineEditMM, &QLineEdit::returnPressed, ui->pushButtonMM, &QPushButton::click);

    connect(ui->pushButtonMH, &QPushButton::clicked, this, &MyWidget::sendBtnHitMH);
    connect(ui->lineEditMH, &QLineEdit::returnPressed, ui->pushButtonMH, &QPushButton::click);

    connect(ui->pushButtonMP, &QPushButton::clicked, this, &MyWidget::sendBtnHitMP);
    connect(ui->lineEditMP, &QLineEdit::returnPressed, ui->pushButtonMP, &QPushButton::click);

    connect(ui->pushButtonQA, &QPushButton::clicked, this, &MyWidget::sendBtnHitQA);
    connect(ui->pushButtonQB, &QPushButton::clicked, this, &MyWidget::sendBtnHitQB);
    connect(ui->pushButtonQC, &QPushButton::clicked, this, &MyWidget::sendBtnHitQC);
    connect(ui->pushButtonQD, &QPushButton::clicked, this, &MyWidget::sendBtnHitQD);

    connect(ui->pushButtonS, &QPushButton::clicked, this, &MyWidget::sendBtnHitS);
//    connect(ui->lineEditQ, &QLineEdit::returnPressed, ui->pushButtonQA, &QPushButton::click);
//    connect(ui->lineEditQ, &QLineEdit::returnPressed, ui->pushButtonQB, &QPushButton::click);
//    connect(ui->lineEditQ, &QLineEdit::returnPressed, ui->pushButtonQC, &QPushButton::click);
//    connect(ui->lineEditQ, &QLineEdit::returnPressed, ui->pushButtonQD, &QPushButton::click);

    ui->stackedWidget->setCurrentIndex(0);
}

MyWidget::~MyWidget() {
    sock->close();
    delete ui;
}

void MyWidget::connectBtnHit(){
    ui->connectGroup->setEnabled(false);
    ui->msgsTextEdit->append("<b>Connecting to " + ui->hostLineEdit->text() + ":" + QString::number(ui->portSpinBox->value())+"</b>");
    sock = new QTcpSocket(this);
    connTimeoutTimer = new QTimer(this);
    connTimeoutTimer->setSingleShot(true);
    connect(connTimeoutTimer, &QTimer::timeout, [&]{
        sock->abort();
        sock->deleteLater();
        connTimeoutTimer->deleteLater();
        ui->connectGroup->setEnabled(true);
        ui->msgsTextEdit->append("<b>Connect timed out</b>");
        QMessageBox::critical(this, "Error", "Connect timed out");
    });

    connect(sock, &QTcpSocket::connected, this, &MyWidget::socketConnected);
    connect(sock, &QTcpSocket::disconnected, this, &MyWidget::socketDisconnected);
    connect(sock, (void(QTcpSocket::*)(QTcpSocket::SocketError)) &QTcpSocket::error, this, &MyWidget::socketError);
    connect(sock, &QTcpSocket::readyRead, this, &MyWidget::socketReadable);

    sock->connectToHost(ui->hostLineEdit->text(), ui->portSpinBox->value());
    connTimeoutTimer->start(3000);
}

void MyWidget::socketConnected(){
    connTimeoutTimer->stop();
    connTimeoutTimer->deleteLater();
    ui->talkGroup->setEnabled(true);
}

void MyWidget::socketDisconnected(){
    ui->stackedWidget->setCurrentIndex(0);
    ui->msgsTextEdit->append("<b>Disconnected</b>");
    ui->talkGroup->setEnabled(false);
}

void MyWidget::socketError(QTcpSocket::SocketError err){
    if(err == QTcpSocket::RemoteHostClosedError)
        return;
    ui->stackedWidget->setCurrentIndex(0);
    QMessageBox::critical(this, "Error", sock->errorString());
    ui->msgsTextEdit->append("<b>Socket error: "+sock->errorString()+"</b>");
    ui->talkGroup->setEnabled(false);
}

void MyWidget::socketReadable(){


    QByteArray ba = sock->readAll();
    if(QString::fromUtf8(ba).trimmed().startsWith("MM:===")){
        ui->stackedWidget->setCurrentIndex(1);
        ui->lineEditMM->setEnabled(true);
        ui->textEditMM->clear();
        ui->textEditMM->append(QString::fromUtf8(ba).trimmed().remove(0,3));
        ui->textEditMM->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("MM:Room does not exist")){
        ui->stackedWidget->setCurrentIndex(1);
        ui->lineEditMM->setEnabled(false);
        ui->textEditMM->clear();
        ui->textEditMM->append(QString::fromUtf8(ba).trimmed().remove(0,3));
        ui->textEditMM->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("MH:Quiz created")){
        ui->stackedWidget->setCurrentIndex(2);
        ui->textEditMH->clear();
        ui->lineEditMH->setEnabled(false);
        ui->textEditMH->append(QString::fromUtf8(ba).trimmed().remove(0,3));
        ui->textEditMH->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("MH")){
        ui->stackedWidget->setCurrentIndex(2);
        ui->textEditMH->clear();
        ui->lineEditMH->setEnabled(true);
        ui->textEditMH->append(QString::fromUtf8(ba).trimmed().remove(0,3));
        ui->textEditMH->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("MP")){
        ui->stackedWidget->setCurrentIndex(3);
        ui->textEditMP->clear();
        ui->textEditMP->append(QString::fromUtf8(ba).trimmed().remove(0,3));
        ui->textEditMP->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("Q")){
        ui->stackedWidget->setCurrentIndex(4);
        ui->groupBox_4->setEnabled(true);
        ui->textEditQ->clear();
        ui->textEditQ->append(QString::fromUtf8(ba).trimmed().remove(0,2));
        ui->textEditQ->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("S")){
        ui->stackedWidget->setCurrentIndex(5);
        ui->textEditS->append(QString::fromUtf8(ba).trimmed().remove(0,2));
        ui->textEditS->setAlignment(Qt::AlignLeft);
    }
    else if(QString::fromUtf8(ba).trimmed().startsWith("Choose your nickname")){
        ui->stackedWidget->setCurrentIndex(0);
        ui->msgsTextEdit->append(QString::fromUtf8(ba).trimmed());
        ui->msgsTextEdit->setAlignment(Qt::AlignLeft);
        ui->msgLineEdit->setFocus();
    }
    else{
        ui->stackedWidget->setCurrentIndex(0);
        ui->msgsTextEdit->append(QString::fromUtf8(ba).trimmed());
        ui->msgsTextEdit->setAlignment(Qt::AlignLeft);
        ui->msgLineEdit->setFocus();
        ui->connectGroup->setEnabled(true);

    }
}

void MyWidget::sendBtnHit(){
    auto txt = ui->msgLineEdit->text().trimmed();
    sock->write((txt+'\n').toUtf8());

    ui->msgsTextEdit->append("<span style=\"color: blue\">"+txt+"</span>");
    ui->msgsTextEdit->setAlignment(Qt::AlignRight);

    ui->msgLineEdit->clear();
    ui->msgLineEdit->setFocus();
}

void MyWidget::sendBtnHitMM(){
    auto txt = ui->lineEditMM->text().trimmed();
    sock->write((txt+'\n').toUtf8());

    ui->textEditMM->append("<span style=\"color: blue\">"+txt+"</span>");
    ui->lineEditMM->clear();
    ui->lineEditMM->setFocus();
}


void MyWidget::sendBtnHitMH(){
    auto txt = ui->lineEditMH->text().trimmed();
    sock->write((txt+'\n').toUtf8());

    ui->textEditMH->append("<span style=\"color: blue\">"+txt+"</span>");
    ui->lineEditMH->clear();
    ui->lineEditMH->setFocus();
}

void MyWidget::sendBtnHitMP(){
    auto txt = ui->lineEditMP->text().trimmed();
    sock->write((txt+'\n').toUtf8());

    ui->textEditMP->append("<span style=\"color: blue\">"+txt+"</span>");
    ui->lineEditMP->clear();
    ui->lineEditMP->setFocus();
}

void MyWidget::sendBtnHitQA(){
    auto txt = ui->pushButtonQA->text().trimmed();
    sock->write((txt+'\n').toUtf8());
    ui->groupBox_4->setEnabled(false);
    ui->textEditQ->clear();
    ui->textEditQ->append("Waiting for other players...");
}

void MyWidget::sendBtnHitQB(){
    auto txt = ui->pushButtonQB->text().trimmed();
    sock->write((txt+'\n').toUtf8());
    ui->groupBox_4->setEnabled(false);
    ui->textEditQ->clear();
    ui->textEditQ->append("Waiting for other players...");
}

void MyWidget::sendBtnHitQC(){
    auto txt = ui->pushButtonQC->text().trimmed();
    sock->write((txt+'\n').toUtf8());
    ui->groupBox_4->setEnabled(false);
    ui->textEditQ->clear();
    ui->textEditQ->append("Waiting for other players...");
}

void MyWidget::sendBtnHitQD(){
    auto txt = ui->pushButtonQD->text().trimmed();
    sock->write((txt+'\n').toUtf8());
    ui->groupBox_4->setEnabled(false);
    ui->textEditQ->clear();
    ui->textEditQ->append("Waiting for other players...");
}

void MyWidget::sendBtnHitS(){
    auto txt = ui->pushButtonS->text().trimmed();
    sock->write((txt+'\n').toUtf8());;
    ui->textEditS->clear();
}
