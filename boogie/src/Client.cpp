#include "Client.h"
#include <iostream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
//192.168.191.128
Client::Client(QObject* parrent)
    :QTcpSocket(parrent)
{
	std::cout << "Client created" << std::endl;
}

void Client::connectToServer(QString ip, quint16 port) {
    connectToHost(ip, port);

    connect(this, SIGNAL(readyRead()), this, SLOT(readMsg()));

    std::cout << "Connected to host" << std::endl;
}

void Client::readMsg(){
    std::cout << "read msg:" << std::endl;
    std::cout << readAll().toStdString() << std::endl;
}

void Client::disconnectFromServer() {
    disconnectFromHost();
    std::cout << "Disconected from host" << std::endl;
}

void Client::sendMsg(QString str) {
    write(str.toLocal8Bit().data());
}

void Client::sendMsgData(QString from, QString to, QString msg) {
    QJsonObject js;
	js.insert("type", "m");
    js.insert("from", from);
    js.insert("to", to);
    js.insert("msg", msg);
    if(!js.empty()) {
        QJsonDocument doc(js);
        QString strJs(doc.toJson(QJsonDocument::Compact));
        QString strJsLen = QString::number(strJs.length());
        while(strJsLen.length() < 4)
            strJsLen = QString::number(0) + strJsLen;
		QString fullMsgString = strJsLen + strJs;
        sendMsg(fullMsgString);
		//qDebug() << strJs << strJs.length() << fullMsgString;
    }
}

void Client::sendAuthData(QString username, QString password){
    QJsonObject js;
	js.insert("type", "a");
    js.insert("password", password);
    js.insert("username", username);
    if(!js.empty()) {
        QJsonDocument doc(js);
        QString strJs(doc.toJson(QJsonDocument::Compact));
        QString strJsLen = QString::number(strJs.length());
        while(strJsLen.length() < 4)
            strJsLen = QString::number(0) + strJsLen;
		QString fullAuthString = strJsLen + strJs;
        sendMsg(fullAuthString);
		//qDebug() << strJs << strJs.length() << fullAuthString;
    }
}
