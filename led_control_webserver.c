/**
 * AULA IoT - Embarcatech - Ricardo Prates - 004 - Webserver Raspberry Pi Pico w - wlan
 *
 * Material de suporte
 * 
 * https://www.raspberrypi.com/documentation/pico-sdk/networking.html#group_pico_cyw43_arch_1ga33cca1c95fc0d7512e7fef4a59fd7475 
 */

#include <stdio.h>               // Biblioteca padrão para entrada e saída
#include <string.h>              // Biblioteca manipular strings
#include <stdlib.h>              // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)

#include "pico/stdlib.h"         // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"        // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "pico/cyw43_arch.h"     // Biblioteca para arquitetura Wi-Fi da Pico com CYW43  

#include "lwip/pbuf.h"           // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

#include "inc/ssd1306.h"         // Display

#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"


// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "Son"
#define WIFI_PASSWORD "14164881j"

// Definição dos pinos
#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE_PIN 12                 // GPIO12 - LED azul
#define LED_GREEN_PIN 11                // GPIO11 - LED verde
#define LED_RED_PIN 13                  // GPIO13 - LED vermelho
// Botões
#define BTN_A 5
#define BTN_B 6
#define BTN_JOY 22

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Configurando Joystick
#define EIXO_Y 26    // ADC0
#define EIXO_X 27    // ADC1
#define PWM_WRAP 4095

// Configurando I2C
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15

// Configurando Display
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define SQUARE_SIZE 8

// Configurando posições do quadrado no display
int pos_x = (DISPLAY_WIDTH - SQUARE_SIZE) / 2;
int pos_y = (DISPLAY_HEIGHT - SQUARE_SIZE) / 2;
const int SPEED = 2;
const int MAX_X = DISPLAY_WIDTH - SQUARE_SIZE;
const int MAX_Y = DISPLAY_HEIGHT - SQUARE_SIZE;

// Declarando variáveis globais
volatile bool pwm_on = false;
volatile bool borda = false;
volatile bool led_r_estado = false;
volatile bool led_g_estado = false;
volatile bool led_b_estado = false;
bool cor = true;
absolute_time_t last_interrupt_time = 0;
float rpm = 0;

// Protótipos de funções
void gpio_callback(uint gpio, uint32_t events);
void JOYSTICK(uint slice1);
void update_menu(uint8_t *ssd, struct render_area *frame_area);

// Flag
volatile char c = '~';
volatile bool new_data = false;
volatile int current_digit = 0;

// Protótipos das funções
void npDisplayDigit(int digit);

// Função auxiliar para processar o comando e atualizar os displays
void process_command(char c, int digit, char *line1, char *line2, uint8_t *ssd, struct render_area *frame_area) {
    if (BTN_A) {
    sleep_ms(5);
    } else {
        printf("...", c);
    } 

    // Atualiza o OLED
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, frame_area);
    ssd1306_draw_string(ssd, 5, 0, line1);
    ssd1306_draw_string(ssd, 5, 8, line2);
    render_on_display(ssd, frame_area);
}


// Funções para o Buzzer

#define BUZZER 21

void init_pwm(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM); // Configura o GPIO como PWM
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice_num, 125.0f);     // Define o divisor do clock para 1 MHz
    pwm_set_wrap(slice_num, 1000);        // Define o TOP para frequência de 1 kHz
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0); // Razão cíclica inicial
    pwm_set_enabled(slice_num, true);     // Habilita o PWM
}   void set_buzzer_tone(uint gpio, uint freq) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    uint top = 1000000 / freq;            // Calcula o TOP para a frequência desejada
    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), top / 2); // 50% duty cycle
}   void stop_buzzer(uint gpio) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), 0); // Desliga o PWM
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Leitura da temperatura interna
float temp_read(void);

// Tratamento do request do usuário
void user_request(char **request);


// Função principal
int main()
{
    //Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Iniciando e configurando os LEDs
    gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
    gpio_init(LED_RED_PIN);
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, 0);
    gpio_put(LED_BLUE_PIN, 0);

    // Iniciando e configurando o Buzzer
    init_pwm(BUZZER);

    //  Iniciando e configurando os botões
    gpio_init(BTN_A);
    gpio_init(BTN_B);
    gpio_init(BTN_JOY);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_set_dir(BTN_JOY, GPIO_IN);
    gpio_pull_up(BTN_A);
    gpio_pull_up(BTN_B);
    gpio_pull_up(BTN_JOY);

    //  Habilitando Interrupção
    gpio_set_irq_enabled(BTN_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_JOY, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_callback(gpio_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);
    
    //  Iniciando ADC
    adc_init();
    adc_gpio_init(EIXO_Y);
    adc_gpio_init(EIXO_X);

    //  Iniciando PWM
    uint slice1 = pwm_gpio_to_slice_num(LED_GREEN_PIN);
    pwm_set_wrap(slice1, PWM_WRAP);
    pwm_set_clkdiv(slice1, 2.0f);
    pwm_set_enabled(slice1, true);

    // Inicialização do i2c
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);


        // Configura a área de renderização do display OLED
        ssd1306_init();

        struct render_area frame_area = {
            .start_column = 0,
            .end_column = ssd1306_width - 1,
            .start_page = 0,
            .end_page = ssd1306_n_pages - 1
        };
        calculate_render_area_buffer_length(&frame_area);
    
        // zera o display inteiro
        uint8_t ssd[ssd1306_buffer_length];
        memset(ssd, 0, ssd1306_buffer_length);
        render_on_display(ssd, &frame_area);
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    gpio_led_bitdog();

    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        JOYSTICK(slice1);    // Lê os eixos do Joystick
                
        if (pwm_on) {
            adc_select_input(1);
            uint16_t pot_value = adc_read();
            pwm_set_gpio_level(LED_GREEN_PIN, pot_value);
        } else {
            pwm_set_gpio_level(LED_GREEN_PIN, 0);
        }
        
        // Atualiza o menu do display
        update_menu(ssd, &frame_area);
        cor = !cor;
        /* 
        * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
        * Este método deve ser chamado periodicamente a partir do ciclo principal 
        * quando se utiliza um estilo de sondagem pico_cyw43_arch 
        */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU

        if (led_r_estado == true || led_b_estado == true) {
        set_buzzer_tone(BUZZER, 395);
        }
        if (led_r_estado == false && led_b_estado == false) {
        stop_buzzer(BUZZER);
        }
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void){
    // Configuração dos LEDs como saída
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_BLUE_PIN, false);
    
    //gpio_init(LED_GREEN_PIN);
    //gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    //gpio_put(LED_GREEN_PIN, false);
    
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, false);
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request){

    const uint16_t CENTER = 2047;
    const uint16_t DEADZONE = 170;  // zona morta do Joystick
    adc_select_input(1);
    uint16_t x_value = adc_read();    
    int16_t x_diff = (int16_t)x_value;// - CENTER;
    float pwm_x = (abs(x_diff) <= DEADZONE) ? 0 : abs(x_diff) ;

    
    if (strstr(*request, "GET /motor_on") != NULL)
    {
        pwm_set_gpio_level(LED_GREEN_PIN, pwm_x);
        pwm_on = !pwm_on;
    }
    /*
    else if (strstr(*request, "GET /motor_off") != NULL)
    {
        pwm_set_gpio_level(LED_GREEN_PIN, 0);
        pwm_on = false;
    }
    */
    else if (strstr(*request, "GET /rotor_on") != NULL)
    {
        led_b_estado = !led_b_estado;
        if (led_b_estado == true){
            gpio_put(LED_BLUE_PIN, true);        
            set_buzzer_tone(BUZZER, 395);
        } else {
            gpio_put(LED_BLUE_PIN, false);
            stop_buzzer(BUZZER);
        }
        
    }
    /*
    else if (strstr(*request, "GET /rotor_off") != NULL)
    {
        gpio_put(LED_BLUE_PIN, 0);
        led_b_estado = false;
        stop_buzzer(BUZZER);
    }
    */
    else if (strstr(*request, "GET /estator_on") != NULL)
    {
        led_r_estado = !led_r_estado;
        if (led_r_estado == true){
            gpio_put(LED_RED_PIN, true);        
            set_buzzer_tone(BUZZER, 395);
        } else {
            gpio_put(LED_RED_PIN, false);
            stop_buzzer(BUZZER);
        }
    }
    /*
    else if (strstr(*request, "GET /estator_off") != NULL)
    {
        gpio_put(LED_RED_PIN, 0);
        led_r_estado = false;
        stop_buzzer(BUZZER);
    }
    */
    else if (strstr(*request, "GET /on") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 1);
    }
    else if (strstr(*request, "GET /off") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 0);
    }
};

// Leitura da temperatura interna
float temp_read(void){
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperature = 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;
    return temperature;
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);
    
    // Leitura da temperatura interna
    float temperature = temp_read();

    // Para exibir Tensão
    const uint16_t CENTRO = 2047;
    const uint16_t ZONAMORTA = 170;  // zona morta do Joystick
    adc_select_input(1);
    uint16_t x_valor = adc_read();    
    int16_t x_dif = (int16_t)x_valor;// - CENTER;
    float tensao_valor = (abs(x_dif) <= ZONAMORTA) ? 0 : abs(x_dif);
    tensao_valor = 440 * (tensao_valor / 4096);

    if (!pwm_on) {
        tensao_valor = 0;
    }

    // Determina o estado do motor baseado na tensão
    char motor_status[15];
    if (x_valor > 3000) {
        strcpy(motor_status, "Sobretensao");
    } else if (x_valor < 1000) {
        strcpy(motor_status, "Subtensao");
    } else {
        strcpy(motor_status, "Normal");
    }

    // Determina o estado do rotor (LED azul)
    char rotor_status[10];
    if (led_b_estado) {
        strcpy(rotor_status, "Falha");
    } else {
        strcpy(rotor_status, "Normal");
    }

    // Determina o estado do estator (LED vermelho)
    char estator_status[10];
    if (led_r_estado) {
        strcpy(estator_status, "Falha");
    } else {
        strcpy(estator_status, "Normal");
    }

    char ligado[10];
    if (pwm_on) {
        strcpy(ligado, "Ligado");
    } else {
        strcpy(ligado, "Desligado");
    }

    // Cria a resposta HTML
    char html[1024]; // Buffer de tamanho original

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title>Embarcatech - Motor Monitor</title>\n"
             "<style>\n"
             "body { background-color:rgb(32, 165, 99); font-family: Arial, sans-serif; text-align: center; margin-top: 20px; }\n"
             "h1 { font-size: 36px; margin-bottom: 20px; }\n"
             //"button { background-color: LightGray; font-size: 24px; width: 350px; margin: 15px 0; padding: 20px; border-radius: 8px; }\n"
             "button {font-size: 24px; width: 350px; padding: 20px; margin: 10px 0; border-radius: 8px;}\n"
             ".status { font-size: 24px; margin: 15px 0; font-weight: bold; }\n"
             ".tensao { font-size: 24px; margin: 15px 0; font-weight: bold; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1>Motor Monitor</h1>\n"
             
             "<form action=\"./motor_on\"><button>(ON/OFF) Motor</button></form>\n"
             "<form action=\"./estator_on\"><button>(ON/OFF) Falha Estator</button></form>\n"
             "<form action=\"./rotor_on\"><button>(ON/OFF) Falha Rotor</button></form>\n"
             "<form action=\"./atualiza\"><button>Atualizar</button></form>\n"
             
             "<div class=\"status\">Motor: %s</div>\n"
             "<p class=\"tensao\">Tensao: %.2f V (%s)</p>\n"
             //"<div class=\"status\">%s</div>\n"
             "<div class=\"status\">Estator: %s</div>\n"
             "<div class=\"status\">Rotor: %s</div>\n"
             
             
             "</body>\n"
             "</html>\n",
             ligado, tensao_valor, motor_status, estator_status, rotor_status);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Função de Callback
void gpio_callback(uint gpio, uint32_t events) {
    absolute_time_t now = get_absolute_time();
    int64_t diff = absolute_time_diff_us(last_interrupt_time, now);

    if (diff < 250000) return;
    last_interrupt_time = now;

    if (gpio == BTN_A) {
        led_r_estado = !led_r_estado;
        gpio_put(LED_RED_PIN, led_r_estado);
        led_r_estado ? printf("Falha no Estator\n") : printf("Estator Normal\n");
        new_data = true;
        if(led_r_estado){c = '#';}else{c = '$';}
    }

    if (gpio == BTN_B) {
        led_b_estado = !led_b_estado;
        gpio_put(LED_BLUE_PIN, led_b_estado);
    }
    
    if (gpio == BTN_JOY) {
        pwm_on = !pwm_on;
    }
}


// Função do Joystick
void JOYSTICK(uint slice1) {
    const uint16_t CENTER = 2047;
    const uint16_t DEADZONE = 170;  // zona morta do Joystick

    // Lê o eixo Y (ADC0)
    adc_select_input(0);
    uint16_t y_value = adc_read();
    
    // Lê o eixo X (ADC1)
    adc_select_input(1);
    uint16_t x_value = adc_read();
    
    int16_t x_diff = (int16_t)x_value;// - CENTER;
    int16_t y_diff = (int16_t)y_value;// - CENTER;

    // Verificação do pwm em relação a deadzone
    uint16_t pwm_y = (abs(y_diff) <= DEADZONE) ? 0 : abs(y_diff) ;
    uint16_t pwm_x = (abs(x_diff) <= DEADZONE) ? 0 : abs(x_diff) ;

    if (pwm_on) {
    //    rpm = (pwm_x * pwm_y) / 4095;
    //    if (pwm_x == 0 || pwm_y == 0) {
    //        rpm = 0;
    //}

        pwm_set_gpio_level(LED_GREEN_PIN, pwm_x);
    } else {
        pwm_set_gpio_level(LED_GREEN_PIN, 0);
    }
}

void update_menu(uint8_t *ssd, struct render_area *frame_area) {
    memset(ssd, 0, ssd1306_buffer_length);

    // Se o LED verde estiver desligado, mostra a mensagem "motor desligado"
    if (!pwm_on) {
        ssd1306_draw_string(ssd, 0, 20, "Motor desligado");
        render_on_display(ssd, frame_area);
        return;
    }
    
    char motor_status[15];
    char rotor_status[10];
    char estator_status[10];

    // Lê o valor do potenciômetro (ADC1) para determinar o estado do motor
    adc_select_input(1);
    uint16_t pot_value = adc_read();
    if (pot_value > 3000) {
        strcpy(motor_status, "sobretensao");
    } else if (pot_value < 1000) {
        strcpy(motor_status, "subtensao");
    } else {
        strcpy(motor_status, "normal");
    }

    // Determina o estado do rotor (LED azul)
    if (led_b_estado) {
        strcpy(rotor_status, "falha");
    } else {
        strcpy(rotor_status, "normal");
    }

    // Determina o estado do estator (LED vermelho)
    if (led_r_estado) {
        strcpy(estator_status, "falha");
    } else {
        strcpy(estator_status, "normal");
    }

    // Organiza o display em 6 linhas (verticalmente com espaçamento de 10 pixels)
    ssd1306_draw_string(ssd, 0, 0, "TENSAO");
    char buffer[30];
    sprintf(buffer, "  %s", motor_status);
    ssd1306_draw_string(ssd, 0, 10, buffer);

    ssd1306_draw_string(ssd, 0, 20, "ROTOR");
    sprintf(buffer, "  %s", rotor_status);
    ssd1306_draw_string(ssd, 0, 30, buffer);

    ssd1306_draw_string(ssd, 0, 40, "ESTATOR");
    sprintf(buffer, "  %s", estator_status);
    ssd1306_draw_string(ssd, 0, 50, buffer);

    render_on_display(ssd, frame_area);
}