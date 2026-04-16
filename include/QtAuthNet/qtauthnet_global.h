#pragma once

#include <QtGlobal>

#if defined(QTAUTHNET_LIBRARY)
#  define QTAUTHNET_EXPORT Q_DECL_EXPORT
#else
#  define QTAUTHNET_EXPORT Q_DECL_IMPORT
#endif
