/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "httplogger.h"

#include <QRegularExpression>
#include <QLoggingCategory>
#include <QBuffer>

namespace {
Q_LOGGING_CATEGORY(lcNetworkHttp, "sync.httplogger", QtWarningMsg)

const qint64 PeekSize = 1024 * 1024;

const QByteArray XRequestId(){
    return QByteArrayLiteral("X-Request-ID");
}

bool isTextBody(const QString &s)
{
    static const QRegularExpression regexp(QStringLiteral("^(text/.*|(application/(xml|json|x-www-form-urlencoded)(;|$)))"));
    return regexp.match(s).hasMatch();
}

void logHttp(bool isRequest, const QByteArray &verb, const QString &url, const QByteArray &id, const QString &contentType, const qint64 &contentLength, const QList<QNetworkReply::RawHeaderPair> &header, QIODevice *device)
{
    QString msg;
    QTextStream stream(&msg);
    stream << id << ": ";
    if (isRequest) {
        stream << "Request: ";
    } else {
        stream << "Response: ";
    }
    stream << verb << " " << url << " Header: { ";
    for (const auto &it : header) {
        stream << it.first << ": ";
        if (it.first == "Authorization") {
            stream << "[redacted]";
        } else {
            stream << it.second;
        }
        stream << ", ";
    }
    stream << "} Data: [";
    if (contentLength > 0) {
        if (isTextBody(contentType)) {
            if (!device->isOpen()) {
                Q_ASSERT(dynamic_cast<QBuffer *>(device));
                // should we close it again?
                device->open(QIODevice::ReadOnly);
            }
            Q_ASSERT(device->pos() == 0);
            stream << device->peek(PeekSize);
            if (PeekSize < contentLength)
            {
                stream << "...(" << (contentLength - PeekSize) << "bytes elided)";
            }
        } else {
            stream << contentLength << " bytes of " << contentType << " data";
        }
    }
    stream << "]";
    qCInfo(lcNetworkHttp) << msg;
}
}


namespace OCC {


void HttpLogger::logReplyOnFinished(const QNetworkReply *reply)
{
    if (!lcNetworkHttp().isInfoEnabled()) {
        return;
    }
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply] {
        logHttp(false,
            requestVerb(*reply),
            reply->url().toString(),
            reply->request().rawHeader(XRequestId()),
            reply->header(QNetworkRequest::ContentTypeHeader).toString(),
            reply->header(QNetworkRequest::ContentLengthHeader).toInt(),
            reply->rawHeaderPairs(),
            const_cast<QNetworkReply *>(reply));
    });
}

void HttpLogger::logRequest(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, QIODevice *device)
{
    if (!lcNetworkHttp().isInfoEnabled()) {
        return;
    }
    const auto keys = request.rawHeaderList();
    QList<QNetworkReply::RawHeaderPair> header;
    header.reserve(keys.size());
    for (const auto &key : keys) {
        header << qMakePair(key, request.rawHeader(key));
    }
    logHttp(true,
        requestVerb(operation, request),
        request.url().toString(),
        request.rawHeader(XRequestId()),
        request.header(QNetworkRequest::ContentTypeHeader).toString(),
        device ? device->size() : 0,
        header,
        device);
}

QByteArray HttpLogger::requestVerb(QNetworkAccessManager::Operation operation, const QNetworkRequest &request)
{
    switch (operation) {
    case QNetworkAccessManager::HeadOperation:
        return QByteArrayLiteral("HEAD");
    case QNetworkAccessManager::GetOperation:
        return QByteArrayLiteral("GET");
    case QNetworkAccessManager::PutOperation:
        return QByteArrayLiteral("PUT");
    case QNetworkAccessManager::PostOperation:
        return QByteArrayLiteral("POST");
    case QNetworkAccessManager::DeleteOperation:
        return QByteArrayLiteral("DELETE");
    case QNetworkAccessManager::CustomOperation:
        return request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
    case QNetworkAccessManager::UnknownOperation:
        break;
    }
    Q_UNREACHABLE();
}

}
