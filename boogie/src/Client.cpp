﻿#include "Client.h"
#include <iostream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QString>
#include <utility>
#include <iterator>
#include <QXmlStreamWriter>
#include <QFile>
#include <QDir>
#include <ctime>
#include <tuple>
#include "../util/util.h"
#include <map>
#include <QCryptographicHash>
#include <QImageReader>
#include <QPixmap>
#include <QBuffer>
#include <QUrl>
#include <iostream>
#include <QImageWriter>

Client::Client(QObject* parrent)
    :QSslSocket(parrent)
{
    //setting ssl certificates
    setLocalCertificate("../certs/blue_local.pem");
    setPrivateKey("../certs/blue_local.key");
    setPeerVerifyMode(QSslSocket::VerifyPeer);
    connect(this,  SIGNAL(sslErrors(QList<QSslError>)), this,
            SLOT(sslErrors(QList<QSslError>)));

    std::cout << "Client created" << std::endl;
}

QString Client::username() {
    return m_username;
}

void Client::sslErrors(const QList<QSslError> &errors)
{
    foreach (const QSslError &error, errors){
        if(error.error() == QSslError::SelfSignedCertificate){//ignoring self signed cert
            QList<QSslError> expectedSslErrors;
            expectedSslErrors.append(error);
            ignoreSslErrors(expectedSslErrors);
        }
    }
}



void Client::connectToServer(const QString& username, const QString& ip,
                             quint16 port) {
    connectToHostEncrypted(ip, port);

    if(waitForEncrypted(3000)){
        qDebug() << "Encrypted connection established";
    }
    else{
        std::cerr << "Unable to connect to server" << std::endl;
        return;
    }
    connect(this, SIGNAL(readyRead()), this, SLOT(readMsg()));

    this->m_username = username;

    QDir dir(m_username);
    if (!dir.exists())
        QDir().mkdir(m_username);
}

void Client::disconnectFromServer() {
    disconnectFromHost();
    std::cout << "Disconected from host" << std::endl;
}

void Client::addNewContact(const QString& name, bool online) {

    if(m_contactInfos[name] != true)
        m_contactInfos[name] = online;
    emit clearContacts();
    for(auto i = m_contactInfos.cbegin(); i != m_contactInfos.cend(); i++){
        emit showContacts(i.key(), i.value());
    }
}

void Client::readMsg(){
    if(m_bytesToRead == 0)//we dont have any unread data
    {
        QByteArray messageLength = read(sizeof(int));
        QDataStream stream(&messageLength, QIODevice::ReadOnly);
        stream.setVersion(QDataStream::Qt_5_10); //to ensure that new version of QDataStream wouldn't change much
        int length;
        stream >> length;
        qDebug() << "ARRIVED: " << length;
        m_bytesToRead = length;
    }
    if(bytesAvailable() < m_bytesToRead){
        qDebug() << "missing " << m_bytesToRead - bytesAvailable() << " bytes";
        return;
    }


    QJsonDocument jsonMsg = QJsonDocument::fromJson(read(m_bytesToRead));
    m_bytesToRead = 0;//we have read all there is for this message

    QJsonObject jsonMsgObj = jsonMsg.object();
    auto msgType = jsonMsgObj["type"];

    if(jsonMsgObj.contains("to") && jsonMsgObj["to"].toString() != m_username) {
        qDebug() << "Received msg is not for " << m_username;
        return;
    }
    if(msgType == MessageType::Text){
        QString message = jsonMsgObj["msg"].toString();

        addMsgToBuffer(jsonMsgObj["from"].toString(),
                        jsonMsgObj["from"].toString(),
                        message,
                        QString("text"));

        emit showMsg(jsonMsgObj["from"].toString(),
                    message);
    }
    else if(msgType == MessageType::Contacts){
        QJsonArray contactsJsonArray = jsonMsgObj["contacts"].toArray();
        for(auto con : contactsJsonArray){
            QJsonObject jsonObj = con.toObject();
            m_contactInfos[jsonObj["contact"].toString()] = jsonObj["online"].toBool();
            //emit showContacts(jsonObj["contact"].toString(), jsonObj["online"].toBool());
        }

        for(auto i = m_contactInfos.cbegin(); i != m_contactInfos.cend(); i++){
            emit showContacts(i.key(), i.value());
        }
    }
    else if(msgType == MessageType::ContactLogin){
        m_contactInfos[jsonMsgObj["contact"].toString()] = true;
        emit clearContacts();
        for(auto i = m_contactInfos.cbegin(); i != m_contactInfos.cend(); i++){
            emit showContacts(i.key(), i.value());
        }
    }
    else if(msgType == MessageType::ContactLogout){
        m_contactInfos[jsonMsgObj["contact"].toString()] = false;
        emit clearContacts();
        for(auto i = m_contactInfos.cbegin(); i != m_contactInfos.cend(); i++){
            emit showContacts(i.key(), i.value());
        }
    }
    else if(msgType == MessageType::AddNewContact) {
        if(jsonMsgObj["exists"].toBool() == true)
            addNewContact(jsonMsgObj["username"].toString(), jsonMsgObj["online"].toBool());
        else {
            emit badContact(QString("Trazeni kontakt ne postoji!"));
        }
    }
    else if (msgType == MessageType::Image){
        // ovako je samo dok ne proradi
        // TODO: popravi kada proradi
        QPixmap p;
        p.loadFromData(QByteArray::fromBase64(jsonMsgObj["msg"].toString().toLatin1()));
        QImage image = p.toImage();
        QString path = m_username + QString("/img") + QString::number(m_imgCounter) + QString(".png");
        QImageWriter writer(path, "png");
        writer.write(image);
        showPicture(jsonMsgObj["from"].toString(), "file://" + QFileInfo(path).absoluteFilePath());
        addMsgToBuffer(jsonMsgObj["from"].toString(),
                        jsonMsgObj["from"].toString(),
                        "file://" + QFileInfo(path).absoluteFilePath(),
                        QString("image"));
        m_imgCounter++;
    }
    else if(msgType == MessageType::BadPass){
        emit badPass();
    }
    else if(msgType == MessageType::AllreadyLoggedIn){
        emit alreadyLogIn();
    }
    else if(msgType == MessageType::BadMessageFormat){
        qDebug() << "Ne znas da programiras :p";
    }
    else{
        qDebug() << "UNKNOWN MESSAGE TYPE";
    }

    //GOTTA CATCH 'EM ALL
    //when multiple messages arive at small time frame,
    //either signal is not emited or slot is not called(not sure)
    //so this is necessary for reading them all
    if(this->bytesAvailable() != 0){
        emit readyRead();
    }
}

void Client::sendMsg(const QString& str) {
    write(str.toLocal8Bit().data());
}

//dodajemo poruku u bafer
void Client::addMsgToBuffer(const QString& sender,const QString& inConversationWith,const QString& msg, const QString& type) {
    //hvatamo trenutno vreme i pretvaramo ga u string zbog lakseg upisivanja u xml
    auto start = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(start);
    std::tm * ptm = std::localtime(&end_time);
    char time[32];
    std::strftime(time, 32, "%d.%m.%Y %H:%M:%S", ptm);
    if(m_msgDataBuffer.find(inConversationWith) == m_msgDataBuffer.end()) {
        m_msgIndexBegin[inConversationWith] = 0;
    }
    auto pair = QPair<QString, std::tuple<QString, QString, QString>>(type, std::make_tuple(sender, msg, time));
    m_msgDataBuffer[inConversationWith].push_back(pair);
    m_msgCounter++;
    if(m_msgCounter == 10) {
        writeInXml();
    }
}

void Client::displayOnConvPage(const QString& inConversationWith) {
    auto messages = m_msgDataBuffer.find(inConversationWith);
    if(messages == m_msgDataBuffer.end())
        return;
    for(auto pair: messages.value()) {
        auto type = pair.first;
        auto message = pair.second;
        qDebug() << type;
        if(type == "text") {
            emit showMsg(std::get<0>(message), std::get<1>(message));
        } else {
            emit showPicture(std::get<0>(message), std::get<1>(message));
        }
    }
}


void Client::createXml() const {
    QString filePath = m_username + "/" + "Messages.xml";
    QFile data(filePath);
          if (!data.open(QFile::WriteOnly | QFile::Truncate))
              return;

    QXmlStreamWriter xml(&data);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();

    QTextStream stream(&data);
    stream << "\n<messages>\n</messages>";

    data.close();
}

// pisemo istoriju ceta u xml fajl
void Client::writeInXml() {
    QString filePath = m_username + "/" + "Messages.xml";

    if(!(QFileInfo::exists(filePath) && QFileInfo(filePath).isFile())) {
        createXml();
    }

    QFile data(filePath);
        if (!data.open(QFile::ReadWrite | QFile::Text))
            return;

    QXmlStreamWriter xml(&data);
    xml.setAutoFormatting(true);

    qint64 offset = data.size() - QString("</messages>").size() - 1;
    data.seek(offset);

    auto messages = m_msgDataBuffer.begin();
    while(messages != m_msgDataBuffer.end())
    {
        auto from = m_msgIndexBegin[messages.key()];
        auto to = messages.value().size();
        for(auto i = from; i < to; i++) {
            auto pair = messages.value()[i];
            auto message = pair.second;
            auto type = pair.first;
            xml.writeStartElement("message");
            xml.writeAttribute("type", type);
            xml.writeTextElement("inConversationWith", messages.key());
            xml.writeTextElement("sender", std::get<0>(message));
            xml.writeTextElement("content", std::get<1>(message));
            xml.writeTextElement("time", std::get<2>(message));
            xml.writeEndElement();
        }
        m_msgIndexBegin[messages.key()] = to;
        messages++;
    }

    QTextStream stream(&data);
    stream << "\n</messages>";
    data.close();
    m_msgCounter = 0;
}

void Client::readFromXml() {
    QString filePath = m_username + "/Messages.xml";

    QFile data(filePath);
    if (!data.open(QFile::ReadOnly))
        return;

    QXmlStreamReader xml(&data);

    while (!xml.atEnd()) {
        xml.readNextStartElement();
        if(xml.isStartElement() && xml.name() == "message") {
            auto attribute = xml.attributes()[0];
            auto type = attribute.value().toString();
            xml.readNextStartElement();
            QString inConversationWith = xml.readElementText();
            xml.readNextStartElement();
            QString sender = xml.readElementText();
            xml.readNextStartElement();
            QString text = xml.readElementText();
            xml.readNextStartElement();
            QString time = xml.readElementText();
            auto pair = QPair<QString, std::tuple<QString, QString, QString>>(type,std::make_tuple(sender, text, time));
            m_msgDataBuffer[inConversationWith].push_back(pair);
            if(m_msgIndexBegin.find(inConversationWith) == m_msgIndexBegin.end())
                m_msgIndexBegin[inConversationWith] = 1;
            else
                m_msgIndexBegin[inConversationWith]++;
            if(type == "image" && sender != m_username) {
                m_imgCounter++;
            }
       }
    }

    data.close();
}

//saljemo serveru username novog kontakta, da bi proverili da li taj kontakt postoji
void Client::checkNewContact(const QString& name) {
    if(name == m_username) {
        badContact(QString("Ne mozete pricati sami sa sobim,\n nije socijalno prihvatljivo"));
    }
    QJsonObject jsonObject;
    jsonObject.insert("type", setMessageType(MessageType::AddNewContact));
    jsonObject.insert("username", name);
    jsonObject.insert("from", this->m_username);
    if(!jsonObject.empty()) {
        QString fullMsgString = packMessage(jsonObject);
        int msgLength = fullMsgString.size();
        QByteArray byteArray;
        QDataStream stream(&byteArray, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_10); //to ensure that new version of DataStream wouldn't change much
        stream << msgLength;
        write(byteArray);
        sendMsg(fullMsgString);
    }
}

void Client::sendPicture(const QString& to, const QString& filePath) {
    QImageReader reader(QUrl(filePath).toLocalFile());
    QImage img = reader.read();
    auto pix = QPixmap::fromImage(img);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    // TODO sta ako nije png
    // TODO ovde naci ekstenziju slike(lastIndexOf("."))
    pix.save(&buffer, "png");
    auto const data = buffer.data().toBase64();

    QJsonObject jsonMessageObject;
    jsonMessageObject.insert("type", setMessageType(MessageType::Image));
    jsonMessageObject.insert("from", m_username);
    jsonMessageObject.insert("to", to);
    jsonMessageObject.insert("id", QString("img") + QString::number(m_imageNum));
    jsonMessageObject.insert("msg", QLatin1String(data));
    qDebug() << "image: " << data.size() << "string: " << QLatin1String(data).size();
    QString msgString = packMessage(jsonMessageObject);
    int msgLength = msgString.size();
    QByteArray byteArray;
    QDataStream stream(&byteArray, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_10); //to ensure that new version of DataStream wouldn't change much
    stream << msgLength;
    write(byteArray);
    sendMsg(msgString);
    flush();
    m_imageNum++;
}

//saljemo poruku i podatke o njoj na server
void Client::sendMsgData(const QString& to,const QString& msg) {
    QJsonObject jsonMessageObject;
    jsonMessageObject.insert("type", setMessageType(MessageType::Text));
    jsonMessageObject.insert("from", m_username);
    jsonMessageObject.insert("to", to);
    jsonMessageObject.insert("msg", msg);
    if(!jsonMessageObject.empty()) {
        QString fullMsgString = packMessage(jsonMessageObject);
        int msgLength = fullMsgString.size();
        QByteArray byteArray;
        QDataStream stream(&byteArray, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_10); //to ensure that new version of DataStream wouldn't change much
        stream << msgLength;
        write(byteArray);
        sendMsg(fullMsgString);
    }
}

//saljemo podatke za proveru username i sifre na server
void Client::sendAuthData(QString password){
    QJsonObject jsonAuthObject;
    //pravimo json objekat u koji smestamo podatke
    jsonAuthObject.insert("type", setMessageType(MessageType::Authentication));
    QString hashedPassword =
            QString(QCryptographicHash::hash((password.toLocal8Bit().data()),
                                             QCryptographicHash::Md5).toHex());
    jsonAuthObject.insert("password", hashedPassword);
    jsonAuthObject.insert("username", m_username);
    if(!jsonAuthObject.empty()) {
        QString fullAuthString = packMessage(jsonAuthObject);
        int msgLength = fullAuthString.size();
        QByteArray byteArray;
        QDataStream stream(&byteArray, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_10); //to ensure that new version of DataStream wouldn't change much
        stream << msgLength;
        write(byteArray);
        sendMsg(fullAuthString);
        //qDebug() << strJson << strJson.length() << fullAuthString;
    }
}

