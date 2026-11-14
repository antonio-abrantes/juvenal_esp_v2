#pragma once
#if __has_include("juvenal_secrets.h")
#include "juvenal_secrets.h"
#else
#include "juvenal_secrets.example.h"
#endif
#if __has_include("juvenal_web_credentials.h")
#include "juvenal_web_credentials.h"
#else
#include "juvenal_web_credentials.example.h"
#endif
#ifndef JUVENAL_AUTO_TRANSPORT_WS
#define JUVENAL_AUTO_TRANSPORT_WS true
#endif
#ifndef JUVENAL_WS_URL
#define JUVENAL_WS_URL ""
#endif
#ifndef JUVENAL_WS_DEVICE_ID
#define JUVENAL_WS_DEVICE_ID "juvenal"
#endif
#ifndef JUVENAL_WS_DEVICE_TOKEN
#define JUVENAL_WS_DEVICE_TOKEN ""
#endif
#ifndef JUVENAL_SPEAKER_ENABLED
#define JUVENAL_SPEAKER_ENABLED false
#endif
#ifndef JUVENAL_TX_BCLK_GPIO
#define JUVENAL_TX_BCLK_GPIO 15
#endif
#ifndef JUVENAL_TX_WS_GPIO
#define JUVENAL_TX_WS_GPIO 16
#endif
#ifndef JUVENAL_TX_DOUT_GPIO
#define JUVENAL_TX_DOUT_GPIO 17
#endif
