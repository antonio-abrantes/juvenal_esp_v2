/* Juvenal Visual v0.3 - painel Web local autenticado.
 * Sem TLS embarcado: APENAS LAN privada. Nunca publicar porta 80.
 * Os endpoints mutaveis exigem X-Juvenal-UI: 1 para reduzir CSRF.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <cmath>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "juvenal_config.h"
#include "juvenal_web.h"
#include "juvenal_speaker.h"
#include "juvenal_ws.h"
#include "juvenal_led.h"

extern const uint8_t juvenal_web_html_start[] asm("_binary_juvenal_web_html_start");

namespace {
static const char *TAG = "JUVENAL_WEB";
static constexpr size_t URL_MAX = 255;
static char s_url[URL_MAX + 1] = {};
static SemaphoreHandle_t s_url_mutex = nullptr;
static httpd_handle_t s_server = nullptr;
static std::atomic<unsigned> s_volume_revision{0}, s_volume_saved{0};
static TickType_t s_volume_attempt;
static esp_err_t s_routes_error = ESP_OK;

static bool valid_url(const char *url)
{
    if (!url) return false;
    const size_t len = std::strlen(url);
    if (len < 12 || len > URL_MAX) return false;
    if (std::strncmp(url,"https://",8) && std::strncmp(url,"http://",7)) return false;
    const char *host = std::strstr(url,"://");
    if (!host || !host[3] || host[3]=='/' || host[3]==' ' ) return false;
    for (const char *p=url;*p;++p)
        if (static_cast<unsigned char>(*p)<=32 || *p=='\x7f' || *p=='\\') return false;
    return true;
}
static void security_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req,"Cache-Control","no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
}
static esp_err_t json(httpd_req_t *req, const char *status, const char *body)
{
    security_headers(req);
    httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    return httpd_resp_sendstr(req,body);
}
static esp_err_t error(httpd_req_t *req,const char *status,const char *message)
{
    char buf[190];
    std::snprintf(buf,sizeof(buf),"{\"message\":\"%s\"}",message);
    return json(req,status,buf);
}
static bool constant_time_equals(const char *a,const char *b)
{
    size_t la=std::strlen(a),lb=std::strlen(b);
    size_t n=std::max(la,lb);
    unsigned diff=static_cast<unsigned>(la^lb);
    for(size_t i=0;i<n;i++) {
        unsigned ac=i<la?static_cast<unsigned char>(a[i]):0;
        unsigned bc=i<lb?static_cast<unsigned char>(b[i]):0;
        diff|=ac^bc;
    }
    return diff==0;
}
static bool authenticated(httpd_req_t *req)
{
    char token[220];
    const size_t hlen=httpd_req_get_hdr_value_len(req,"Authorization");
    if(hlen==0 || hlen>=sizeof(token)) goto unauthorized;
    if(httpd_req_get_hdr_value_str(req,"Authorization",token,sizeof(token))!=ESP_OK)
        goto unauthorized;
    {
        char credentials[180];
        const int n=std::snprintf(credentials,sizeof(credentials),"%s:%s",JUVENAL_WEB_USERNAME,JUVENAL_WEB_PASSWORD);
        if(n<=0 || n>=static_cast<int>(sizeof(credentials))) goto unauthorized;
        char base64[260];
        size_t encoded=0;
        if(mbedtls_base64_encode(reinterpret_cast<unsigned char*>(base64),sizeof(base64)-1,
            &encoded,reinterpret_cast<const unsigned char*>(credentials),n)!=0) goto unauthorized;
        base64[encoded]=0;
        char expected[270];
        if(std::snprintf(expected,sizeof(expected),"Basic %s",base64)>=static_cast<int>(sizeof(expected)))
            goto unauthorized;
        if(constant_time_equals(token,expected)) return true;
    }
unauthorized:
    security_headers(req);
    httpd_resp_set_hdr(req,"WWW-Authenticate","Basic realm=\"Juvenal local\", charset=\"UTF-8\"");
    httpd_resp_set_status(req,"401 Unauthorized");
    httpd_resp_set_type(req,"text/plain");
    httpd_resp_sendstr(req,"Autenticacao necessaria");
    return false;
}
static bool valid_mutation(httpd_req_t *req)
{
    char hdr[8];
    return httpd_req_get_hdr_value_str(req,"X-Juvenal-UI",hdr,sizeof(hdr))==ESP_OK &&
           std::strcmp(hdr,"1")==0;
}
static esp_err_t get_home(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    security_headers(req);
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,reinterpret_cast<const char*>(juvenal_web_html_start),HTTPD_RESP_USE_STRLEN);
}
static esp_err_t get_config(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    char url[URL_MAX+1];juvenal_web_get_url(url,sizeof(url));
    cJSON *doc=cJSON_CreateObject();
    if(!doc)return error(req,"500 Internal Server Error","Memoria insuficiente");
    cJSON_AddStringToObject(doc,"url",url);
    char *response=cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if(!response)return error(req,"500 Internal Server Error","Memoria insuficiente");
    esp_err_t result=json(req,"200 OK",response);
    cJSON_free(response);
    return result;
}
static esp_err_t post_config(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    if(!valid_mutation(req))return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if(req->content_len<=0||req->content_len>450)return error(req,"400 Bad Request","Corpo invalido");
    char body[451];int received=0;
    while(received<req->content_len) {
        int r=httpd_req_recv(req,body+received,req->content_len-received);
        if(r==HTTPD_SOCK_ERR_TIMEOUT)continue;
        if(r<=0)return error(req,"400 Bad Request","Falha ao receber formulario");
        received+=r;
    }
    body[received]=0;
    cJSON *doc=cJSON_Parse(body);
    if(!doc)return error(req,"400 Bad Request","JSON invalido");
    const cJSON *field=cJSON_GetObjectItemCaseSensitive(doc,"url");
    bool ok=cJSON_IsString(field)&&valid_url(field->valuestring) && juvenal_web_set_url(field->valuestring);
    cJSON_Delete(doc);
    if(!ok)return error(req,"400 Bad Request","URL invalida ou falha ao salvar NVS");
    return json(req,"200 OK","{\"message\":\"Webhook salvo na NVS.\"}");
}
static esp_err_t get_volume(httpd_req_t *req)
{
    if (!authenticated(req)) return ESP_OK;
    char body[160];
    std::snprintf(body, sizeof(body), "{\"volume\":%u,\"persisted\":%s}",
                  juvenal_speaker_get_volume(),
                  s_volume_revision.load() == s_volume_saved.load() ? "true" : "false");
    return json(req, "200 OK", body);
}
static esp_err_t post_volume(httpd_req_t *req)
{
    if (!authenticated(req)) return ESP_OK;
    if (!valid_mutation(req)) return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if (req->content_len <= 0 || req->content_len > 64)
        return error(req,"400 Bad Request","Corpo invalido");
    char body[65]; int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return error(req,"408 Request Timeout","Formulario incompleto");
        received += count;
    }
    body[received] = 0;
    cJSON *doc = cJSON_ParseWithLengthOpts(body, received + 1, nullptr, true);
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(doc, "volume");
    const bool valid = cJSON_IsObject(doc) && doc->child == field && field && !field->next &&
        cJSON_IsNumber(field) && std::isfinite(field->valuedouble) &&
        field->valuedouble >= 0 && field->valuedouble <= 100 &&
        std::floor(field->valuedouble) == field->valuedouble;
    const unsigned value = valid ? static_cast<unsigned>(field->valuedouble) : 0;
    cJSON_Delete(doc);
    if (!valid) return error(req,"400 Bad Request","Volume deve ser inteiro entre 0 e 100");
    if (juvenal_speaker_get_volume() != value) {
        juvenal_speaker_set_volume(value);
        s_volume_revision.fetch_add(1);
    }
    // Flash/NVS so entre interacoes; nunca dentro do callback I2S.
    return json(req,"200 OK","{\"message\":\"Volume aplicado. Sera salvo quando o Juvenal estiver livre.\"}");
}

static esp_err_t post_start(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    if(!valid_mutation(req))return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if(!juvenal_capture_web_start())return error(req,"409 Conflict","Juvenal ocupado. Libere a gravacao anterior.");
    return json(req,"200 OK","{\"message\":\"Gravacao iniciada (maximo 6 segundos).\"}");
}
static esp_err_t post_stop(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    if(!valid_mutation(req))return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if(!juvenal_capture_web_stop())return error(req,"409 Conflict","Nenhuma gravacao manual em andamento");
    return json(req,"200 OK","{\"message\":\"Parada solicitada. Aguarde a finalizacao.\"}");
}
static esp_err_t post_clear(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    if(!valid_mutation(req))return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if(!juvenal_capture_web_clear())return error(req,"409 Conflict","Nao ha gravacao pronta para descartar");
    return json(req,"200 OK","{\"message\":\"Audio apagado. Wake word reativada.\"}");
}
static esp_err_t post_send(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    if(!valid_mutation(req))return error(req,"403 Forbidden","Cabecalho de seguranca ausente");
    if(!juvenal_capture_web_send())return error(req,"409 Conflict","Gravacao ocupada ou sem recursos para criar tarefa de envio");
    return json(req,"202 Accepted","{\"message\":\"Envio iniciado. O resultado aparecera no painel.\"}");
}
static esp_err_t get_status(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    char buf[1024];juvenal_capture_web_status(buf,sizeof(buf));
    const size_t used = std::strlen(buf);
    if (!used || buf[used - 1] != '}') return error(req,"500 Internal Server Error","Status indisponivel");
    const int count = std::snprintf(buf + used - 1, sizeof(buf) - used + 1,
        ",\"volume\":%u,\"volume_saved\":%s,\"boot_state\":\"%s\"}",
        juvenal_speaker_get_volume(),
        s_volume_revision.load() == s_volume_saved.load() ? "true" : "false",
        juvenal_led_state_name());
    if (count < 0 || size_t(count) >= sizeof(buf) - used + 1)
        return error(req,"500 Internal Server Error","Status excedeu limite");
    return json(req,"200 OK",buf);
}
static esp_err_t get_audio(httpd_req_t *req)
{
    if(!authenticated(req))return ESP_OK;
    const unsigned char *data=nullptr;size_t size=0;
    if(!juvenal_capture_web_audio(&data,&size))return error(req,"409 Conflict","Nenhum WAV gravado");
    security_headers(req);
    httpd_resp_set_type(req,"audio/wav");
    httpd_resp_set_hdr(req,"Content-Disposition","inline; filename=juvenal.wav");
    for(size_t pos=0;pos<size;) {
        size_t chunk=std::min(static_cast<size_t>(4096),size-pos);
        if(httpd_resp_send_chunk(req,reinterpret_cast<const char*>(data+pos),chunk)!=ESP_OK)
            return ESP_FAIL;
        pos+=chunk;
    }
    return httpd_resp_send_chunk(req,nullptr,0);
}
static void add_uri(const char *path,httpd_method_t method,esp_err_t(*cb)(httpd_req_t*))
{
    httpd_uri_t uri={};uri.uri=path;uri.method=method;uri.handler=cb;
    esp_err_t err=httpd_register_uri_handler(s_server,&uri);
    if(err!=ESP_OK) { s_routes_error = err; ESP_LOGE(TAG,"Rota %s falhou: %s",path,esp_err_to_name(err)); }
}
} // namespace

extern "C" esp_err_t juvenal_web_init(void)
{
    if(!s_url_mutex)s_url_mutex=xSemaphoreCreateMutex();
    if(!s_url_mutex) {ESP_LOGE(TAG,"Sem RAM para mutex da URL");return ESP_ERR_NO_MEM;}
    std::snprintf(s_url,sizeof(s_url),"%s",JUVENAL_WEBHOOK_URL);
    nvs_handle_t nvs;
    const esp_err_t opened = nvs_open("juvenal_web",NVS_READONLY,&nvs);
    if (opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND) return opened;
    if(opened == ESP_OK) {
        char saved[URL_MAX+1];size_t len=sizeof(saved);
        if(nvs_get_str(nvs,"webhook",saved,&len)==ESP_OK && valid_url(saved))
            std::snprintf(s_url,sizeof(s_url),"%s",saved);
        uint8_t saved_volume = 100;
        if (nvs_get_u8(nvs, "volume", &saved_volume) == ESP_OK && saved_volume <= 100)
            juvenal_speaker_set_volume(saved_volume);
        nvs_close(nvs);
    }
    return ESP_OK;
}
extern "C" void juvenal_web_get_url(char *out,size_t size)
{
    if(!out||size==0)return;
    if(!s_url_mutex){std::snprintf(out,size,"%s",JUVENAL_WEBHOOK_URL);return;}
    xSemaphoreTake(s_url_mutex,portMAX_DELAY);
    std::snprintf(out,size,"%s",s_url);
    xSemaphoreGive(s_url_mutex);
}
extern "C" bool juvenal_web_set_url(const char *url)
{
    if(!valid_url(url)||!s_url_mutex)return false;
    nvs_handle_t nvs;
    if(nvs_open("juvenal_web",NVS_READWRITE,&nvs)!=ESP_OK)return false;
    esp_err_t err=nvs_set_str(nvs,"webhook",url);
    if(err==ESP_OK)err=nvs_commit(nvs);
    nvs_close(nvs);
    if(err!=ESP_OK)return false;
    xSemaphoreTake(s_url_mutex,portMAX_DELAY);
    std::snprintf(s_url,sizeof(s_url),"%s",url);
    xSemaphoreGive(s_url_mutex);
    ESP_LOGI(TAG,"Webhook atualizado no painel (NVS)");
    return true;
}
extern "C" esp_err_t juvenal_web_start(void)
{
    if(!s_url_mutex){ESP_LOGE(TAG,"Painel desabilitado: NVS/mutex indisponivel");return ESP_ERR_INVALID_STATE;}
    if(std::strcmp(JUVENAL_WEB_PASSWORD,"COLOQUE_UMA_SENHA_FORTE_AQUI")==0 ||
       std::strlen(JUVENAL_WEB_PASSWORD)<12 || !JUVENAL_WEB_USERNAME[0]) {
        ESP_LOGE(TAG,"Painel web NAO iniciado: configure uma senha de pelo menos 12 caracteres em juvenal_web_credentials.h");
        return ESP_ERR_INVALID_ARG;
    }
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.server_port=80;
    cfg.max_uri_handlers=11;
    cfg.stack_size=8192;
    cfg.lru_purge_enable=true;
    cfg.recv_wait_timeout=10;
    cfg.send_wait_timeout=20;
    esp_err_t err=httpd_start(&s_server,&cfg);
    if(err!=ESP_OK){ESP_LOGE(TAG,"Servidor web falhou: %s",esp_err_to_name(err));return err;}
    add_uri("/",HTTP_GET,get_home);
    add_uri("/api/config",HTTP_GET,get_config);
    add_uri("/api/config",HTTP_POST,post_config);
    add_uri("/api/status",HTTP_GET,get_status);
    add_uri("/api/record/start",HTTP_POST,post_start);
    add_uri("/api/record/stop",HTTP_POST,post_stop);
    add_uri("/api/record/clear",HTTP_POST,post_clear);
    add_uri("/api/record/send",HTTP_POST,post_send);
    add_uri("/api/audio",HTTP_GET,get_audio);
    add_uri("/api/volume",HTTP_GET,get_volume);
    add_uri("/api/volume",HTTP_POST,post_volume);
    if (s_routes_error != ESP_OK) { httpd_stop(s_server); s_server = nullptr; return s_routes_error; }
    ESP_LOGI(TAG,"Painel web LAN pronto: HTTP porta 80 (Basic Auth; NAO publicar na Internet)");
    return ESP_OK;
}

extern "C" bool juvenal_web_settings_pending(void)
{
    return s_volume_revision.load() != s_volume_saved.load();
}

extern "C" void juvenal_web_flush_settings(void)
{
    const unsigned revision = s_volume_revision.load();
    const TickType_t now = xTaskGetTickCount();
    if (revision == s_volume_saved.load() || juvenal_ws_active() ||
        now - s_volume_attempt < pdMS_TO_TICKS(2000)) return;
    s_volume_attempt = now;
    const uint8_t value = static_cast<uint8_t>(juvenal_speaker_get_volume());
    nvs_handle_t handle;
    esp_err_t result = nvs_open("juvenal_web", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, "volume", value);
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result == ESP_OK) s_volume_saved.store(revision);
    else ESP_LOGW(TAG, "Volume aplicado; persistencia pendente: %s", esp_err_to_name(result));
}
