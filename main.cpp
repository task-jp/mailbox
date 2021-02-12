#include <QtCore>
#include <QtNetwork>
#include <QtSerialPort>

class MailBox : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int status READ status WRITE setStatus NOTIFY statusChanged)
public:
    explicit MailBox(const QByteArray &channelAccessToken, const QString &to, QObject *parent = nullptr)
        : QObject(parent)
        , channelAccessToken(channelAccessToken)
        , to(to)
    {
        connect(this, &MailBox::statusChanged, [this](int status) {
qDebug() << "status" << status;
            if (status == 0)
                notify();
        });
    }

    int status() const { return m_status; }

public slots:
    void setStatus(int status) {
        if (m_status == status) return;
        m_status = status;
        emit statusChanged(status);
    }

signals:
    void statusChanged(int status);

private slots:
    void notify() {
        static QNetworkAccessManager nam;
        QNetworkRequest request(QUrl("https://api.line.me/v2/bot/message/push"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + channelAccessToken);
        request.setAttribute(QNetworkRequest::SynchronousRequestAttribute, true);
        QJsonObject content {
            { "to", to },
            { "messages", QJsonArray {
                    QJsonObject {
                        { "type", "text" },
                        { "text", "Incoming mail" }
                    }
                }
            }
        };
        auto reply = nam.post(request, QJsonDocument(content).toJson());
        qDebug() << reply->readAll();
        reply->deleteLater();
    }
private:
    QByteArray channelAccessToken;
    QString to;
    int m_status = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const auto findSerialPortInfo = []() {
        for (const auto &serialPortInfo : QSerialPortInfo::availablePorts()) {
            if (serialPortInfo.manufacturer() == "MONOWIRELESS") {
qDebug() << serialPortInfo.portName();
                return serialPortInfo;
            }
        }
        return QSerialPortInfo();
    };

    const auto serialPortInfo = findSerialPortInfo();
    if (serialPortInfo.isNull()) {
        qWarning() << "serialport not found";
        return -__LINE__;
    }

    QSerialPort serialPort(serialPortInfo);
    serialPort.setBaudRate(115200);
    serialPort.setDataBits(QSerialPort::Data8);
    serialPort.setParity(QSerialPort::NoParity);
    serialPort.setStopBits(QSerialPort::OneStop);
    serialPort.setFlowControl(QSerialPort::NoFlowControl);
    if (!serialPort.open(QSerialPort::ReadOnly)) {
        qWarning() << serialPort.errorString();
        return -__LINE__;
    }

    QByteArray channelAccessToken = qEnvironmentVariable("MAILBOX_CHANNEL_ACCESS_TOKEN").toLatin1();
    QString to = qEnvironmentVariable("MAILBOX_TO");

    MailBox mailBox(channelAccessToken, to);
    QObject::connect(&serialPort, &QSerialPort::readyRead, [&serialPort, &mailBox] {
        static QByteArray buffer;
        while (serialPort.bytesAvailable() > 0) {
            buffer.append(serialPort.readAll());
        }
        while (!buffer.isEmpty()) {
            const auto crlf = buffer.indexOf("\r\n");
            if (crlf < 0)
                break;
            const auto line = buffer.left(crlf);
            buffer = buffer.mid(crlf + 2);
            // https://wings.twelite.info/how-to-use/parent-mode/receive-message/app_pal#sensparu
            const QRegularExpression regexp(
                        "^:"
                        "([0-9A-F]{8})" // 中継機のシリアルID
                        "([0-9A-F]{2})" // LQI
                        "([0-9A-F]{4})" // 続き番号
                        "([0-9A-F]{8})" // 送信元のシリアルID
                        "([0-9A-F]{2})" // 送信元の論理デバイスID
                        "([0-9A-F]{2})" // センサー種別
                        "([0-9A-F]{2})" // PAL基板バージョンとPAL基板ID
                        "([0-9A-F]{2})" // センサーデータの数
                        "([0-9A-F]{2})" // 各種情報ビット値
                        "([0-9A-F]{2})" // データソース
                        "([0-9A-F]{2})" // 拡張バイト
                        "([0-9A-F]{2})" // データ長
                        "([0-9A-F]{4})" // データ
                        "([0-9A-F]{2})" // 各種情報ビット値
                        "([0-9A-F]{2})" // データソース
                        "([0-9A-F]{2})" // 拡張バイト
                        "([0-9A-F]{2})" // データ長
                        "([0-9A-F]{4})" // データ
                        "([0-9A-F]{2})" // 各種情報ビット値
                        "([0-9A-F]{2})" // データソース
                        "([0-9A-F]{2})" // 拡張バイト
                        "([0-9A-F]{2})" // データ長
                        "([0-9A-F]{2})" // データ
                        "([0-9A-F]{2})" // チェックサム1
                        "([0-9A-F]{2})" // チェックサム2
                        "$");
            const auto match = regexp.match(line);
            qDebug() << line;
            if (match.hasMatch()) {
            	qDebug() << match;
                int i = 1;
                const auto receiverSerialID = match.captured(i++).toInt(nullptr, 0x10);
                const auto lqi = match.captured(i++).toInt(nullptr, 0x10);
                const auto sequence = match.captured(i++).toInt(nullptr, 0x10);
                const auto senderSerialID = match.captured(i++).toInt(nullptr, 0x10);
                const auto senderLogicalDeviceID = match.captured(i++).toInt(nullptr, 0x10);
                const auto sensorType = match.captured(i++).toInt(nullptr, 0x10);
                const auto palVersion = match.captured(i++).toInt(nullptr, 0x10);
                const auto sensorDataCount = match.captured(i++).toInt(nullptr, 0x10);
                for (int j = 0; j < sensorDataCount; j++) {
                    const auto dataSize = match.captured(i++).toInt(nullptr, 0x10);
                    const auto dataSource = match.captured(i++).toInt(nullptr, 0x10);
                    const auto dataType = match.captured(i++).toInt(nullptr, 0x10);
                    const auto dataLength = match.captured(i++).toInt(nullptr, 0x10);
                    const auto dataValue = match.captured(i++).toInt(nullptr, 0x10);
                    switch (dataSource) {
                    case 0x30: // ADC
                        switch (dataType) {
                        case 0x01: // ADC1
                            break;
                        case 0x08: // 電源電圧
                            break;
                        }
                        break;
                    case 0x00: // 磁気
                        switch (dataType) {
                        case 0x00:
qDebug() << j << dataValue << (dataValue & 0x80);
                            mailBox.setStatus(dataValue & 0x0F);
                            break;
                        }
                        break;
                    }
                }
                const auto checksum1 = match.captured(i++).toLong(nullptr, 0x10);
                const auto checksum2 = match.captured(i++).toInt(nullptr, 0x10);
            } else {
                qWarning() << line << "is not handled";
            }
        }
    });
    return app.exec();
}

#include "main.moc"
