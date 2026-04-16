#pragma once

#include <QtGlobal>
#include <QtCore/QMetaType>
#include <functional>

#if defined(QTAUTHNET_LIBRARY)
#  define QTAUTHNET_EXPORT Q_DECL_EXPORT
#else
#  define QTAUTHNET_EXPORT Q_DECL_IMPORT
#endif

// 让 std::function 可存入 QVariant（必须在所有 include Qt 头之后）
Q_DECLARE_METATYPE(std::function<void(const QByteArray&)>)
Q_DECLARE_METATYPE(std::function<void(const QString&)>)
Q_DECLARE_METATYPE(std::function<void(bool)>)
Q_DECLARE_METATYPE(std::function<QString()>)
