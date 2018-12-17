#ifndef CLIENT_H
#define CLIENT_H

#include <QTcpSocket>
class Client : public QTcpSocket
{
    Q_OBJECT
public:
	Client(QObject* parent = nullptr);
    Q_INVOKABLE void connectToServer(QString ip, quint16 port = 10000);
    Q_INVOKABLE void sendMsg(QString str);
    Q_INVOKABLE void sendAuthData(QString username, QString password);
    Q_INVOKABLE void sendMsgData(QString from, QString to, QString msg);
    void disconnectFromServer();

signals:
    void showMsg(QString msgFrom, QString msg);

public slots:
    void readMsg();
};

#endif // CLIENT_H
