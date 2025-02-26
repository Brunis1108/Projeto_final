#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "ws2812.pio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include "inc/font.h"
#include <math.h> // Importa a função ceil() para arredondamento
#include "hardware/adc.h"
#include "hardware/pwm.h"

// Definições para o display SSD1306 (comunicação I2C)
#define I2C_PORT i2c1          // Porta I2C utilizada
#define PIN_I2C_SDA 14         // Pino SDA (dados) do I2C
#define PIN_I2C_SCL 15         // Pino SCL (clock) do I2C
#define endereco 0x3C          // Endereço I2C do display OLED

#define buttonA 5              // Pino do botão A
#define buttonB 6              // Pino do botão B

#define MATRIX_LED 7           // Pino da matriz de LEDs
#define NUM_PIXELS 25          // Número de LEDs na matriz (5x5)

// Definição dos pinos do joystick
#define analogicox 27          // Pino do eixo X do joystick (GPIO 27)
#define analogicoy 26          // Pino do eixo Y do joystick (GPIO 26)
#define botao_joystick 22      // Pino do botão do joystick (GPIO 22)

#define servo 20               // Pino do servo motor
#define buzzer 10              // Pino do buzzer

static uint32_t last_time = 0; // Variável para armazenar o tempo da última interrupção
PIO pio = pio0;                // Instância do PIO (Programmable I/O)
uint sm = 0;                   // State machine do PIO
ssd1306_t ssd;                 // Estrutura para o display SSD1306
uint32_t led_buffer[NUM_PIXELS]; // Buffer para os LEDs da matriz
uint16_t eixo_y = 0;           // Variável para armazenar o valor do eixo Y do joystick

bool menu = false;             // Flag para indicar se o menu está ativo
int qtd_racao = 1000;          // Quantidade de ração disponível (em gramas)
int qtd_agua = 1000;           // Quantidade de água disponível (em ml)
int gramas_alimento = 50;      // Quantidade de ração a ser liberada por despejo (em gramas)
int ml_agua = 30;              // Quantidade de água a ser liberada por despejo (em ml)
const char *menu_options[] = {"Manual/Auto", "Racao/Agua", "Encher", "Voltar"}; // Opções do menu
int menu_index = 0;            // Índice da opção selecionada no menu
int num_options = 4;           // Número de opções no menu
bool medir_gramas = false;     // Flag para indicar que é necessário medir a ração
int digit_index = 0;           // Índice do dígito atual durante a edição
int number_digits[3] = {0, 0, 0}; // Array para armazenar os dígitos durante a edição
bool editing = true;           // Flag para indicar que está em modo de edição
enum
{
    STATE_RACAO, // Estado para definir a quantidade de ração
    STATE_AGUA,  // Estado para definir a quantidade de água
    STATE_DONE   // Estado finalizado
};
int state = STATE_RACAO;       // Estado inicial: definir a quantidade de ração

bool modo_auto = false;        // Flag para indicar se o modo automático está ativo
int tempo_auto_ms = 5000;      // Intervalo de tempo para o modo automático (em milissegundos)
struct repeating_timer timer;  // Estrutura para o timer repetitivo

const float period = 20000;    // Período do PWM (em microssegundos)
const float divider_pwm = 125.0f; // Divisor de frequência do PWM

// Protótipos das funções
void button_init(int pin);
void debounce(uint gpio, uint32_t events);
void matrix_init();
void atualizar_leds();
void atualizar_barras();
void display_init();
void iniciar_adc();
bool botao_joystick_pressionado();
void atualizar_display_menu();
void atualizar_menu_com_joystick();
bool alimentar_automatico(struct repeating_timer *t);
void setup_pwm(int pin);
void update_number_display();
void navigate_digits();
void adjust_digit();
void confirm_number();
void despejar();
void manual_automatico();
void play_tone(int pin, uint32_t frequency, uint32_t duration_ms);
void play_sound(int f1, int f2, int t1, int t2);

int main()
{
    stdio_init_all(); // Inicializa a comunicação serial (para debug)
    button_init(buttonA); // Inicializa o botão A
    button_init(buttonB); // Inicializa o botão B
    button_init(botao_joystick); // Inicializa o botão do joystick
    matrix_init(); // Inicializa a matriz de LEDs
    display_init(); // Inicializa o display OLED
    iniciar_adc(); // Inicializa o ADC (para o joystick)
    setup_pwm(servo); // Configura o PWM para o servo motor

    // Configura o pino do buzzer como saída
    gpio_init(buzzer);
    gpio_set_dir(buzzer, GPIO_OUT);

    pwm_set_gpio_level(servo, 2400); // Define o nível do PWM (duty cycle) no pino do servo

    // Configura interrupções para os botões A e B
    gpio_set_irq_enabled_with_callback(buttonA, GPIO_IRQ_EDGE_FALL, true, &debounce);
    gpio_set_irq_enabled_with_callback(buttonB, GPIO_IRQ_EDGE_FALL, true, &debounce);
    sleep_ms(2000); // Aguarda 2 segundos para estabilização
    play_sound(523, 880, 100, 200); // Toca um som de inicialização

    while (true)
    {
        atualizar_barras(); // Atualiza as barras de ração e água no display
        atualizar_leds();   // Atualiza a matriz de LEDs

        // Verifica se o menu está ativo
        if (menu)
        {
            atualizar_menu_com_joystick(); // Atualiza o menu com base no joystick
            ssd1306_draw_string(&ssd, ".>click", 70, 48); // Exibe uma mensagem no display
            ssd1306_send_data(&ssd); // Envia os dados para o display

            // Verifica se o botão do joystick foi pressionado
            if (botao_joystick_pressionado())
            {
                // Executa a lógica para a opção selecionada no menu
                switch (menu_index)
                {
                case 0:
                    manual_automatico(); // Alterna entre modo manual e automático
                    break;

                case 1:
                    editing = true; // Entra no modo de edição
                    while (true)
                    {
                        update_number_display(); // Atualiza o display com o número
                        navigate_digits();       // Navega entre os dígitos
                        adjust_digit();          // Ajusta o dígito atual
                        confirm_number();        // Confirma e salva o número
                        if (state == STATE_DONE)
                        {
                            state = STATE_RACAO; // Reinicia o estado para a próxima vez
                            break;
                        }
                        sleep_ms(50); // Aguarda 50ms
                    }
                    break;

                case 2:
                    printf("Encher selecionado\n");
                    qtd_racao = 1000; // Enche a ração
                    qtd_agua = 1000;  // Enche a água
                    printf("Racao/agua cheios\n");
                    ssd1306_fill(&ssd, false); // Limpa o display
                    ssd1306_draw_string(&ssd, "Racao/agua", 5, 20); // Exibe mensagem
                    ssd1306_draw_string(&ssd, "cheios", 5, 30); // Exibe mensagem
                    ssd1306_send_data(&ssd); // Envia os dados para o display
                    sleep_ms(3000); // Aguarda 3 segundos
                    break;

                case 3:
                    printf("Sair selecionado\n");
                    menu = false; // Sai do menu
                    ssd1306_fill(&ssd, false); // Limpa o display
                    ssd1306_send_data(&ssd); // Envia os dados para o display
                    menu_index = 0; // Reinicia o índice do menu
                    break;
                }
            }
        }
        else
        {
            bool borda = true;
            borda = !borda;
            char racao[20];
            char agua[20];
            // Atualiza o conteúdo do display com animações
            ssd1306_fill(&ssd, !borda);                       // Limpa o display
            ssd1306_rect(&ssd, 3, 3, 122, 58, borda, !borda); // Desenha um retângulo
            sprintf(racao, "Racao: %d g", qtd_racao); // Formata a string da ração
            sprintf(agua, "Agua: %d ml", qtd_agua);   // Formata a string da água

            ssd1306_draw_string(&ssd, racao, 8, 10);     // Exibe a quantidade de ração
            ssd1306_draw_string(&ssd, agua, 8, 20);      // Exibe a quantidade de água
            ssd1306_draw_string(&ssd, "A>racao", 3, 48); // Exibe instrução para o botão A
            ssd1306_draw_string(&ssd, "B>Menu", 75, 48); // Exibe instrução para o botão B
            ssd1306_send_data(&ssd); // Envia os dados para o display
        }

        // Verifica se é necessário liberar ração
        if (medir_gramas)
        {
            despejar(); // Libera a ração e água
            medir_gramas = false; // Reseta a flag
        }
        sleep_ms(300); // Aguarda 300ms antes de atualizar novamente
    }
}

// Função para inicializar um botão
void button_init(int pin)
{
    gpio_init(pin); // Inicializa o pino
    gpio_set_dir(pin, GPIO_IN); // Configura o pino como entrada
    gpio_pull_up(pin); // Habilita o pull-up interno
}

// Função de debounce para os botões
void debounce(uint gpio, uint32_t events)
{
    uint32_t current_time = to_us_since_boot(get_absolute_time()); // Obtém o tempo atual

    // Verifica se o tempo desde a última interrupção é maior que 200ms (debouncing)
    if (current_time - last_time > 200000)
    {
        last_time = current_time; // Atualiza o tempo da última interrupção

        if (gpio == buttonA && !modo_auto)
        {
            medir_gramas = true; // Ativa a liberação de ração no modo manual
        }

        if (gpio == buttonB)
        {
            menu = true; // Ativa o menu
        }
    }
}

// Função para inicializar a matriz de LEDs
void matrix_init()
{
    uint offset = pio_add_program(pio, &ws2812_program); // Adiciona o programa PIO para os LEDs
    ws2812_program_init(pio, sm, offset, MATRIX_LED, 800000, false); // Inicializa a matriz de LEDs
    pio_sm_set_enabled(pio, sm, true); // Habilita a state machine do PIO
    sleep_ms(100); // Aguarda 100ms para estabilização
}

// Função para atualizar os LEDs da matriz
void atualizar_leds()
{
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        pio_sm_put_blocking(pio, sm, led_buffer[i]); // Envia os dados para cada LED
    }
}

// Função para atualizar as barras de ração e água no display
void atualizar_barras()
{
    // Limpa o buffer de LEDs
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        led_buffer[i] = 0; // Desliga todos os LEDs
    }

    // Calcula o número de LEDs acesos para a ração
    int leds_racao = (int)ceil((qtd_racao * 5.0) / 1000.0);
    int indices_racao[] = {4, 5, 14, 15, 24}; // Índices da coluna da ração
    for (int i = 0; i < leds_racao && i < 5; i++)
    {
        led_buffer[indices_racao[i]] = 0x26000000; // Define a cor verde para os LEDs da ração
    }

    // Calcula o número de LEDs acesos para a água
    int leds_agua = (int)ceil((qtd_agua * 5.0) / 1000.0);
    int indices_agua[] = {2, 7, 12, 17, 22}; // Índices da coluna da água
    for (int i = 0; i < leds_agua && i < 5; i++)
    {
        led_buffer[indices_agua[i]] = 0x00002600; // Define a cor azul para os LEDs da água
    }
}

// Função para inicializar o display OLED
void display_init()
{
    // Inicializa a comunicação I2C
    i2c_init(I2C_PORT, 400 * 1000); // Configura o I2C a 400kHz
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C); // Configura o pino SDA
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C); // Configura o pino SCL
    gpio_pull_up(PIN_I2C_SDA); // Habilita pull-up no pino SDA
    gpio_pull_up(PIN_I2C_SCL); // Habilita pull-up no pino SCL

    // Inicializa o display SSD1306
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display

    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);
}

// Função para inicializar o ADC
void iniciar_adc()
{
    adc_init(); // Inicializa o ADC
    adc_gpio_init(analogicox); // Configura o pino do eixo X do joystick como entrada analógica
    adc_gpio_init(analogicoy); // Configura o pino do eixo Y do joystick como entrada analógica
}

// Função para verificar se o botão do joystick foi pressionado
bool botao_joystick_pressionado()
{
    static uint32_t last_time = 0;
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (!gpio_get(botao_joystick)) // Verifica se o botão foi pressionado
    {
        if (current_time - last_time > 200000) // 200ms para debounce
        {
            last_time = current_time;
            return true;
        }
    }
    return false;
}

// Função para atualizar o menu com base na posição do joystick
void atualizar_menu_com_joystick()
{
    adc_select_input(0); // Seleciona o canal ADC correspondente ao eixo Y
    uint16_t eixo_y = adc_read(); // Lê o valor do eixo Y

    // Navega no menu com base no valor do eixo Y
    if (eixo_y < (2047 - 500))
    {
        menu_index = (menu_index + 1) % num_options; // Move para a próxima opção
    }
    else if (eixo_y > (2047 + 500))
    {
        menu_index = (menu_index - 1 + num_options) % num_options; // Move para a opção anterior
    }

    atualizar_display_menu(); // Atualiza o display com o menu
}

// Função para atualizar o display com o menu
void atualizar_display_menu()
{
    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_draw_string(&ssd, "Menu:", 1, 1); // Exibe o título do menu

    // Exibe as opções do menu
    for (int i = 0; i < num_options; i++)
    {
        if (i == menu_index)
        {
            ssd1306_draw_string(&ssd, ">", 1, 13 + i * 10); // Marca a opção selecionada
        }
        ssd1306_draw_string(&ssd, menu_options[i], 10, 13 + i * 10); // Exibe a opção
    }

    ssd1306_send_data(&ssd); // Envia os dados para o display
}

// Função para alternar entre modo manual e automático
void manual_automatico()
{
    char modo[20];
    modo_auto = !modo_auto; // Alterna o modo
    printf("Modo alterado para: %s\n", modo_auto ? "AUTO" : "MANUAL");

    if (!modo_auto)
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Definido: ", 5, 20);
        ssd1306_draw_string(&ssd, "Modo manual", 5, 30);
        ssd1306_send_data(&ssd);
        sleep_ms(3000);
    }

    if (modo_auto)
    {
        printf("Defina o tempo do modo automático...\n");

        while (true)
        {
            adc_select_input(1); // Seleciona o canal ADC correspondente ao eixo Y
            uint16_t eixo_y = adc_read(); // Lê o valor do eixo Y

            // Ajusta o tempo do modo automático com base no joystick
            if (eixo_y < (2047 - 200))
            {
                tempo_auto_ms -= 1000; // Diminui o tempo (mínimo de 1s)
                if (tempo_auto_ms < 1000)
                    tempo_auto_ms = 1000;
            }
            else if (eixo_y > (2047 + 200))
            {
                tempo_auto_ms += 1000; // Aumenta o tempo
                if (tempo_auto_ms > 23000)
                    tempo_auto_ms = 23000; // Máximo 23 horas
            }

            // Atualiza o display com o tempo
            ssd1306_fill(&ssd, false);
            char buffer[20];
            sprintf(buffer, "Tempo: %d h", tempo_auto_ms / 1000);
            ssd1306_draw_string(&ssd, buffer, 10, 20);
            ssd1306_send_data(&ssd);

            sleep_ms(300);

            // Se pressionar o botão do joystick, salva e sai
            if (botao_joystick_pressionado())
            {
                sprintf(modo, "definido: %d h\n", tempo_auto_ms / 1000);

                ssd1306_fill(&ssd, false);
                ssd1306_draw_string(&ssd, "Modo Automatico", 5, 20);
                ssd1306_draw_string(&ssd, modo, 5, 30);
                ssd1306_send_data(&ssd);
                sleep_ms(3000);

                 //tempo_auto_ms = tempo_auto_ms*60*60; //tranforma de milissegundos para horas
                 
                // Inicia o temporizador no modo automático
                cancel_repeating_timer(&timer);
                add_repeating_timer_ms(tempo_auto_ms, alimentar_automatico, NULL, &timer);
                break;
            }
        }
    }
    else
    {
        // Se voltou para Manual, cancela o temporizador automático
        cancel_repeating_timer(&timer);
    }
}

// Função para configurar o PWM
void setup_pwm(int pin)
{
    gpio_set_function(pin, GPIO_FUNC_PWM); // Configura o pino como saída PWM
    uint slice = pwm_gpio_to_slice_num(pin); // Obtém o slice do PWM
    pwm_set_wrap(slice, period); // Define o período do PWM
    pwm_set_clkdiv(slice, divider_pwm); // Define o divisor de frequência
    pwm_set_gpio_level(servo, 0); // Define o nível inicial do PWM
    pwm_set_enabled(slice, true); // Habilita o PWM
}

// Função para atualizar o display com o número
void update_number_display()
{
    ssd1306_fill(&ssd, false); // Limpa o display

    if (editing)
    {
        if (state == STATE_RACAO)
        {
            ssd1306_draw_string(&ssd, "Definir Racao:", 5, 5); // Exibe "Definir Racao:"
        }
        else if (state == STATE_AGUA)
        {
            ssd1306_draw_string(&ssd, "Definir Agua:", 5, 5); // Exibe "Definir Agua:"
        }

        // Exibe os dígitos do número
        char number_str[4];
        sprintf(number_str, "%d%d%d", number_digits[0], number_digits[1], number_digits[2]);
        ssd1306_draw_string(&ssd, number_str, 5, 20);

        // Exibe um indicador para o dígito atual
        char indicator[4] = "    ";
        indicator[digit_index] = '^';
        ssd1306_draw_string(&ssd, indicator, 5, 30);

        ssd1306_send_data(&ssd); // Envia os dados para o display
    }
}

// Função para navegar entre os dígitos
void navigate_digits()
{
    adc_select_input(1); // Seleciona o canal ADC correspondente ao eixo X
    uint16_t eixo_x = adc_read(); // Lê o valor do eixo X

    if (eixo_x < (2047 - 500)) // Joystick inclinado para a esquerda
    {
        digit_index = (digit_index - 1 + 3) % 3; // Move para o dígito anterior
    }
    else if (eixo_x > (2047 + 500)) // Joystick inclinado para a direita
    {
        digit_index = (digit_index + 1) % 3; // Move para o próximo dígito
    }
    sleep_ms(100); // Aguarda 75ms
}

// Função para ajustar o dígito atual
void adjust_digit()
{
    adc_select_input(0); // Seleciona o canal ADC correspondente ao eixo Y
    uint16_t eixo_y = adc_read(); // Lê o valor do eixo Y
    int index_final = digit_index == 0 ? 6 : 10; // Define o limite do dígito
    if (eixo_y < (2047 - 500)) // Joystick inclinado para cima
    {
        number_digits[digit_index] = (number_digits[digit_index] - 1 + index_final) % index_final; // Aumenta o dígito
    }
    else if (eixo_y > (2047 + 500)) // Joystick inclinado para baixo
    {
        number_digits[digit_index] = (number_digits[digit_index] + 1) % index_final; // Diminui o dígito
    }
    sleep_ms(150); // Aguarda 75ms
}

// Função para confirmar o número
void confirm_number()
{
    if (botao_joystick_pressionado())
    {
        // Converte os dígitos em um número inteiro
        int numero = number_digits[0] * 100 + number_digits[1] * 10 + number_digits[2];

        if (state == STATE_RACAO)
        {
            char racao[20];

            if (numero == 0)
            {
                gramas_alimento = 50; // Define um valor padrão
            }
            else if(numero >500){
                gramas_alimento = 500; // Define um valor máximo
            }
            else
            {
                gramas_alimento = numero; // Salva a quantidade de ração
            }
            sprintf(racao, "Racao: %dg\n", gramas_alimento);

            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, racao, 5, 20);
            ssd1306_send_data(&ssd);
            sleep_ms(3000);

            state = STATE_AGUA; // Muda para o estado de definir a quantidade de água
            for (int i = 0; i < 3; i++)
            {
                number_digits[i] = 0; // Reinicia os dígitos
            }
            digit_index = 0; // Reinicia o índice do dígito
        }
        else if (state == STATE_AGUA)
        {
            char agua[20];
            if (numero == 0)
            {
                ml_agua = 30; // Define um valor padrão
            }
            else if(numero>500){
                ml_agua = 500; // Define um valor máximo
            }
            else
            {
                ml_agua = numero; // Salva a quantidade de água
            }

            sprintf(agua, "Agua: %dml\n", ml_agua);

            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, agua, 5, 20);
            ssd1306_send_data(&ssd);
            sleep_ms(3000);

            state = STATE_DONE; // Finaliza o processo
            menu_index = 0; // Volta ao menu
            editing = false; // Sai do modo de edição
        }

        update_number_display(); // Atualiza o display
    }
}

// Função para liberar ração e água
void despejar()
{
    if (qtd_racao - gramas_alimento > 0 && qtd_agua - ml_agua > 0)
    {
        char agua[20];
        char racao[20];
        sprintf(racao, "%dg/racao\n", gramas_alimento);
        sprintf(agua, "%dml/agua\n", ml_agua);
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Adicionado:", 5, 20);
        ssd1306_draw_string(&ssd, racao, 5, 30);
        ssd1306_draw_string(&ssd, agua, 5, 40);
        ssd1306_send_data(&ssd);
        sleep_ms(2000);
        play_sound(220, 392, 200, 300); // Toca um som de confirmação

        pwm_set_gpio_level(servo, 1450); // Move o servo para liberar ração
        sleep_ms(500);
        int aux_qtd_racao = qtd_racao;
        int aux_qtd_agua = qtd_agua;

        // Simula a liberação de ração
        while (qtd_racao >= aux_qtd_racao - gramas_alimento && aux_qtd_racao - gramas_alimento >= 0)
        {
            qtd_racao -= rand() % 6; // Reduz a quantidade de ração
            printf("racao: %d\n", qtd_racao);
            sleep_ms(100);
        }

        pwm_set_gpio_level(servo, 500); // Move o servo para liberar água
        sleep_ms(500);

        // Simula a liberação de água
        while (qtd_agua >= aux_qtd_agua - ml_agua && aux_qtd_agua - ml_agua >= 0)
        {
            qtd_agua -= rand() % 10; // Reduz a quantidade de água
            printf("agua: %d\n", qtd_agua);
            sleep_ms(100);
        }

        pwm_set_gpio_level(servo, 2400); // Retorna o servo à posição inicial
        sleep_ms(100);
    }
    else
    {
        if (qtd_racao - gramas_alimento < 0)
        {
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, "Racao", 5, 20);
            ssd1306_draw_string(&ssd, "Insuficiente", 5, 30);
            ssd1306_send_data(&ssd);
            printf("Racao insuficiente");
        }
        if (qtd_agua - ml_agua < 0)
        {
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, "Agua", 5, 20);
            ssd1306_draw_string(&ssd, "Insuficiente", 5, 30);
            ssd1306_send_data(&ssd);
            printf("Agua insuficiente");
        }
        play_sound(262, 262, 150, 200); // Toca um som de alerta
        sleep_ms(3000);
    }
}

// Função para alimentar automaticamente
bool alimentar_automatico(struct repeating_timer *t)
{
    medir_gramas = true; // Ativa a liberação de ração
    return true;
}

// Função para gerar um tom
void play_tone(int pin, uint32_t frequency, uint32_t duration_ms)
{
    uint32_t period_us = 1000000 / frequency; // Período em microssegundos
    uint32_t half_period_us = period_us / 2;  // Metade do período

    uint32_t start_time = to_us_since_boot(get_absolute_time()); // Tempo inicial
    while (to_us_since_boot(get_absolute_time()) - start_time < duration_ms * 1000)
    {
        gpio_put(pin, 1);         // Liga o buzzer
        sleep_us(half_period_us); // Espera metade do período
        gpio_put(pin, 0);         // Desliga o buzzer
        sleep_us(half_period_us); // Espera metade do período
    }
}

// Função para tocar um som
void play_sound(int f1, int f2, int t1, int t2)
{
    uint32_t frequencies[] = {f1, f2}; // Frequências dos tons
    uint32_t durations[] = {t1, t2};   // Durações dos tons

    for (int i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++)
    {
        play_tone(buzzer, frequencies[i], durations[i]); // Toca cada tom
        sleep_ms(50);                                    // Pequena pausa entre os tons
    }
}