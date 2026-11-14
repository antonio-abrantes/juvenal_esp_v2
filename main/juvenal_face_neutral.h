#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A API da face mantem o nome antigo para nao alterar a inicializacao existente. */
typedef enum {
    JUVENAL_FACE_WAKE = 1,      /* Sustinho + tres piscadas de LED verde */
    JUVENAL_FACE_SENDING = 2,   /* Ouvindo/concluindo captura: LED azul */
    JUVENAL_FACE_SUCCESS = 3,   /* Uma piscadinha + sorriso, LED verde */
    JUVENAL_FACE_ERROR = 4,     /* Rostinho triste, LED vermelho */
    JUVENAL_FACE_SOUND_WAKE = 5, /* Acordar por som, sem acao/LED verde */
    JUVENAL_FACE_TEST_RECORDING = 6, /* Audio teste web: apenas azul */
    JUVENAL_FACE_IDLE = 7,      /* Voltar ao repouso */
    JUVENAL_FACE_LISTENING = 8  /* Escuta conversacional, sem nova wake */
} juvenal_face_event_t;

esp_err_t juvenal_face_neutral_init(void);
/* Seguro chamar de outra task: apenas publica o evento atomico.
   Nunca chama API LVGL nem escreve no LED diretamente. */
void juvenal_face_event(juvenal_face_event_t event);
/* Seguro chamar da task I2S para despertar apenas quando dormindo. */
bool juvenal_face_is_sleeping(void);

#ifdef __cplusplus
}
#endif
