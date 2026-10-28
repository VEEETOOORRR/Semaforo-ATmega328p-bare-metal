#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <avr/interrupt.h>

// =========================================================================
// Tempos configuráveis (em segundos)
// =========================================================================
#define TEMPO_VERDE_CARROS     15
#define TEMPO_AMARELO_CARROS   1
#define TEMPO_VERMELHO_CARROS  8
#define TEMPO_CORTE_BOTAO      3

// =========================================================================
// Máquina de Estado
// =========================================================================
typedef enum {
    ESTADO_CARRO_VERDE,
    ESTADO_CARRO_AMARELO,
    ESTADO_CARRO_VERMELHO
} EstadoSemaforo;

volatile EstadoSemaforo estado_atual = ESTADO_CARRO_VERDE;
volatile uint8_t tempo_restante = TEMPO_VERDE_CARROS;
volatile uint8_t atualizar_leds = 1; // Flag para atualizar portas no main()

// =========================================================================
// Interrupção Externa (INT0) - Botão do Pedestre
// =========================================================================
ISR(INT0_vect) {
    // 1. Desabilita a própria interrupção INT0 para ignorar os "quiques" (bounces)
    EIMSK &= ~(1 << INT0);
    
    // 2. Inicia o Timer0 com Prescaler de 1024
    TCNT0 = 0; // Zera a contagem
    TCCR0B = (1 << CS02) | (1 << CS00); 
}

// =========================================================================
// Interrupção do Timer0 (Ocorre ~16ms após o botão ser apertado)
// =========================================================================
ISR(TIMER0_COMPA_vect) {
    // 1. Para o Timer0 (corta o sinal de clock)
    TCCR0B = 0;
    
    // 2. Verifica se o botão AINDA está pressionado (nível baixo no PD2)
    if (!(PIND & (1 << PD2))) {
        // Ação confirmada! Executa a lógica de cortar o tempo do semáforo
        if (estado_atual == ESTADO_CARRO_VERDE && tempo_restante > TEMPO_CORTE_BOTAO) {
            tempo_restante = TEMPO_CORTE_BOTAO;
        }
    }
    
    // 3. O ruído do botão pode ter gerado falsos gatilhos na flag do INT0 enquanto 
    // ele estava desativado. Limpamos a flag escrevendo '1' nela (particularidade do AVR)
    EIFR |= (1 << INTF0);
    
    // 4. Reabilita o INT0 para aguardar o próximo aperto de botão
    EIMSK |= (1 << INT0);
}

// =========================================================================
// Interrupção do Timer1 (Dispara a cada 1 segundo exato)
// =========================================================================
ISR(TIMER1_COMPA_vect) {
    if (tempo_restante > 0) {
        tempo_restante--;
    }
    
    // Quando o tempo zera, transita para o próximo estado
    if (tempo_restante == 0) {
        switch (estado_atual) {
            case ESTADO_CARRO_VERDE:
                estado_atual = ESTADO_CARRO_AMARELO;
                tempo_restante = TEMPO_AMARELO_CARROS;
                break;
            case ESTADO_CARRO_AMARELO:
                estado_atual = ESTADO_CARRO_VERMELHO;
                tempo_restante = TEMPO_VERMELHO_CARROS;
                break;
            case ESTADO_CARRO_VERMELHO:
                estado_atual = ESTADO_CARRO_VERDE;
                tempo_restante = TEMPO_VERDE_CARROS;
                break;
        }
        atualizar_leds = 1; // Avisa o loop principal que os LEDs precisam mudar
    }
}

// =========================================================================
// Função para configurar os pinos e registradores
// =========================================================================
void setup_hardware() {
    // [As configurações dos LEDs DDRB e PORTB continuam iguais]
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2) | (1 << PB3) | (1 << PB4);
    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) | (1 << PB3) | (1 << PB4));

    // [A configuração do botão DDRD e PORTD continua igual]
    DDRD &= ~(1 << PD2);
    PORTD |= (1 << PD2);

    // Configuração da Interrupção Externa (INT0)
    EICRA |= (1 << ISC01); 
    EICRA &= ~(1 << ISC00);
    EIMSK |= (1 << INT0);  

    // ================= NOVO: Configuração do Timer0 (Debounce) =================
    TCCR0A = (1 << WGM01); // Configura Modo CTC
    OCR0A = 250;           // (16MHz / 1024) = 15625 Hz. 250 ciclos = ~16 ms de espera.
    TIMSK0 |= (1 << OCIE0A); // Habilita a interrupção de comparação do Timer0
    // NOTA: Não configuramos o TCCR0B aqui. Ele começa desligado e a INT0 o liga.

    // Configuração do Timer1 (1 segundo do semáforo)
    TCCR1A = 0; 
    TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);
    OCR1A = 15624; 
    TIMSK1 |= (1 << OCIE1A); 
}

// =========================================================================
// Função para acionar os LEDs baseados no estado atual
// =========================================================================
void rotina_leds() {
    // Zera todos os LEDs antes de ligar os corretos
    PORTB &= ~((1 << PB0) | (1 << PB1) | (1 << PB2) | (1 << PB3) | (1 << PB4));

    switch (estado_atual) {
        case ESTADO_CARRO_VERDE:
            PORTB |= (1 << PB0); // Liga Carro Verde (D8)
            PORTB |= (1 << PB4); // Liga Pedestre Vermelho (D12)
            break;
            
        case ESTADO_CARRO_AMARELO:
            PORTB |= (1 << PB1); // Liga Carro Amarelo (D9)
            PORTB |= (1 << PB4); // Liga Pedestre Vermelho (D12)
            break;
            
        case ESTADO_CARRO_VERMELHO:
            PORTB |= (1 << PB2); // Liga Carro Vermelho (D10)
            PORTB |= (1 << PB3); // Liga Pedestre Verde (D11)
            break;
    }
}

int main() {
    setup_hardware(); // Inicializa pinos, timer e interrupções
    
    sei(); // Habilita interrupções globais

    while(1) {
        // A lógica de tempo acontece de forma invisível nas interrupções.
        // O loop principal só se preocupa em acionar o hardware quando o estado muda.
        if (atualizar_leds) {
            rotina_leds();
            atualizar_leds = 0;
        }
        
        // Aqui o microcontrolador fica ocioso, ideal para implementar modo Sleep
    }
}