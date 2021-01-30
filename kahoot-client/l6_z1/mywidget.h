#ifndef MYWIDGET_H
#define MYWIDGET_H

#include <QWidget>
#include <QTcpSocket>
#include <QTimer>

namespace Ui {
class MyWidget;
}

class MyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MyWidget(QWidget *parent = 0);
    ~MyWidget();

protected:
    QTcpSocket * sock;
    QTimer * connTimeoutTimer;
    void connectBtnHit();
    void socketConnected();
    void socketDisconnected();
    void socketError(QTcpSocket::SocketError);
    void socketReadable();
    void sendBtnHit();
    void sendBtnHitMM();
    void sendBtnHitMH();
    void sendBtnHitMP();

    void sendBtnHitQA();
    void sendBtnHitQB();
    void sendBtnHitQC();
    void sendBtnHitQD();

    void sendBtnHitS();

private:
    Ui::MyWidget * ui;


};

#endif // MYWIDGET_H
