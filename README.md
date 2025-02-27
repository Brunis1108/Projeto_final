# Alimentador Automático com RP2040

Este projeto implementa um alimentador automático utilizando o microcontrolador RP2040 com a placa BitDogLab. Ele permite alternar entre modos manual e automático, configurar tempos de alimentação e quantidades de ração e água.

## Funcionalidades
- Modo **Manual**: O usuário pode liberar ração e água conforme necessidade.
- Modo **Automático**: Alimentação periódica conforme intervalo definido pelo usuário.
- Controle via **joystick analógico** para ajustar quantidades e tempo de alimentação.
- Exibição de informações no **display OLED SSD1306**.
- Uso de um **servo motor** para controle da distribuição de ração.
- Sinalização sonora através de um **buzzer**.

## Componentes Utilizados
- RP2040 (BitDogLab)
- Display OLED SSD1306 (I2C)
- Matriz 5x5 de LEDs WS2812 (GPIO 7)
- LED RGB (GPIOs 11, 12, 13)
- Joystick analógico (ADC - GPIOs 26 e 27)
- Servo motor (PWM - GPIO 15)
- Buzzer (GPIO 14)
- Botões (GPIOs 5 e 6)

## Como Usar
### Alternar Modo Manual/Automático
- Pressione um botão para alternar entre os modos.
- No modo **automático**, defina o intervalo de tempo com o joystick e confirme pressionando o botão.

### Definir Quantidades
- Ao entrar na configuração, ajuste os valores de ração e água usando o joystick.
- Confirme a seleção pressionando o botão.

### Alimentação Automática
- Quando ativado, o dispositivo libera a quantidade definida de ração e água em intervalos programados.

## Estrutura do Código
### Principais Funções
```c
void manual_automatico(); // Alterna entre modo manual e automático
void setup_pwm(int pin); // Configura um pino para PWM
void update_number_display(); // Atualiza a exibição dos valores no display
void navigate_digits(); // Permite navegar entre os dígitos do valor ajustado
void adjust_digit(); // Ajusta os valores usando o joystick
void confirm_number(); // Confirma e salva as configurações
void despejar(); // Libera a ração e a água conforme configurado
bool alimentar_automatico(struct repeating_timer *t); // Executa a alimentação periódica no modo automático
void play_sound(int f1, int f2, int t1, int t2); // Emite sinais sonoros de confirmação e alerta
```

## Observação
- Caso a quantidade de ração ou água seja insuficiente, um alerta é exibido no display e um som é emitido.
- O tempo mínimo para alimentação automática é de **1 hora**, e o máximo é de **23 horas**.

## Link Video demosntrativo
<https://www.youtube.com/watch?v=QmUcH2DhQYo>
## Link documentação
<[https://drive.google.com/file/d/1HCGAT1xfdXry16hNGw6caPRl86esvzJ6/view?usp=drive_link](https://drive.google.com/file/d/1ZZCyQqvxmFqOQ45R95ObMww2DlTTHCv_/view?usp=drive_link)>


