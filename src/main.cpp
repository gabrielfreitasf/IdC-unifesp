#include <Arduino.h>
#include <ModbusMaster.h>

// ===== DEFINIÇÕES DOS PINOS =====
#define PINO_AQUECIMENTO_NO2  2     // Saída digital para aquecimento do sensor NO2
#define PINO_ANALOGICO_NO2    0     // Entrada analógica para leitura do sensor NO2
#define PINO_RS485_DE_RE      3     // Pinos DE e RE ligados juntos para MAX3485
#define RS485_SERIAL          Serial1 // Serial hardware para RS485

// ===== CONFIGURAÇÃO DO SENSOR NO2 =====
#define TEMPO_PRE_AQUECIMENTO_MS  60000  // 60 segundos de pré-aquecimento (ajuste conforme datasheet)
#define INTERVALO_AMOSTRAGEM      2000   // 2 segundos entre leituras
#define TENSAO_REFERENCIA_NO2     3.3    // Tensão de referência para o ADC
#define RESOLUCAO_ADC             4095   // Resolução do ADC de 12 bits
#define TENSAO_AR_LIMPO_NO2       0.4    // Tensão em ar limpo (ajuste conforme sensor)
#define SENSIBILIDADE_NO2         0.35   // mV/ppm (ajuste conforme datasheet do sensor)

// ===== CONFIGURAÇÃO MODBUS =====
#define BAUDRATE_MODBUS   9600   // Baudrate padrão para Modbus RTU
#define BITS_DADOS_MODBUS 8
#define BITS_PARADA_MODBUS 1
#define PARIDADE_MODBUS   SERIAL_PARITY_NONE

// IDs dos escravos Modbus (MUDAR PARA O QUE CONSTA NO DATASHEET)
#define ID_NO_CO          1     // Slave ID para o sensor CO
#define ID_NO_CO2         2     // Slave ID para o sensor CO2

// Endereços de registradores genéricos (você irá modificar estes)
#define REGISTRADOR_HOLDING_INICIO 0x0000
#define REGISTRADOR_INPUT_INICIO   0x0000

// ===== OBJETOS GLOBAIS =====
ModbusMaster sensorCO;
ModbusMaster sensorCO2;

// ===== VARIÁVEIS GLOBAIS =====
unsigned long ultimoTempoAmostragem = 0;
bool preAquecido = false;
unsigned long tempoInicioPreAquecimento = 0;

// Leituras dos sensores
float no2_ppm = 5;
uint16_t valor_bruto_co = 0;    //CHECAR SE ESSE TIPO É ADEQUADO PARA O DADO QUE O SENSOR ENTREGA
uint16_t valor_bruto_co2 = 0;

// ===== PROTÓTIPOS DAS FUNÇÕES =====
void inicializarSensorNO2();
void inicializarRS485();
float lerConcentracaoNO2();
uint16_t lerRegistradorModbus(ModbusMaster &no, uint16_t enderecoRegistrador);
void lerSensores();
void imprimirDadosSensores();

// ===== SETUP =====
void setup() {
    // Inicializa serial para depuração
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== Sistema Inicializando ===");

    // Inicializa sensor NO2
    inicializarSensorNO2();

    // Inicializa RS485 e Modbus
    inicializarRS485();

    Serial.println("Sistema inicializado com sucesso!");
    Serial.println("Iniciando ciclo de pré-aquecimento...");
}

// ===== LOOP PRINCIPAL =====
void loop() {
    // Gerencia o pré-aquecimento do sensor NO2
    if (!preAquecido) {
        if (millis() - tempoInicioPreAquecimento >= TEMPO_PRE_AQUECIMENTO_MS) {
            preAquecido = true;
            digitalWrite(PINO_AQUECIMENTO_NO2, LOW); // Desliga aquecedor (ou mantém ligado dependendo do sensor)
            Serial.println("Pré-aquecimento concluído! Sensor pronto.");
            Serial.println("=== Iniciando leituras contínuas ===");
        }
        return;
    }

    // Realiza amostragem dos sensores no intervalo definido
    if (millis() - ultimoTempoAmostragem >= INTERVALO_AMOSTRAGEM) {
        lerSensores();
        imprimirDadosSensores();
        ultimoTempoAmostragem = millis();
    }

    delay(10);
}

// ===== FUNÇÕES DO SENSOR NO2 =====
void inicializarSensorNO2() {
    pinMode(PINO_AQUECIMENTO_NO2, OUTPUT);
    pinMode(PINO_ANALOGICO_NO2, INPUT);
    
    // Inicia o pré-aquecimento
    digitalWrite(PINO_AQUECIMENTO_NO2, HIGH);
    tempoInicioPreAquecimento = millis();
    Serial.println("Pré-aquecimento do sensor NO2 iniciado...");
}

float lerConcentracaoNO2() {
    // Leitura do valor analógico
    int valorBruto = analogRead(PINO_ANALOGICO_NO2);
    valorBruto = 2*valorBruto
    
    // Conversão para tensão
    float tensao = (valorBruto / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA_NO2;
    
    // Cálculo da concentração de NO2 em ppm
    // Fórmula: ppm = (Vout - Vlimpo) / Sensibilidade
    // A sensibilidade é tipicamente em mV/ppm
    float ppm = (tensao - TENSAO_AR_LIMPO_NO2) * 1000.0 / SENSIBILIDADE_NO2;
    
    // Garante que o valor não seja negativo
    if (ppm < 0) ppm = 0;
    
    return ppm;
}

// ===== FUNÇÕES RS485 E MODBUS =====
void inicializarRS485() {
    // Configuração dos pinos DE/RE
    pinMode(PINO_RS485_DE_RE, OUTPUT);
    digitalWrite(PINO_RS485_DE_RE, LOW); // Padrão: modo recepção
    
    // Inicializa a serial hardware para RS485
    RS485_SERIAL.begin(BAUDRATE_MODBUS, SERIAL_8N1, RX, TX);
    
    // Inicializa os nós Modbus
    sensorCO.begin(ID_NO_CO, RS485_SERIAL);
    sensorCO2.begin(ID_NO_CO2, RS485_SERIAL);
    
    Serial.println("RS485 e Modbus inicializados");
}

// Função para ler um registrador holding de um escravo Modbus
uint16_t lerRegistradorModbus(ModbusMaster &no, uint16_t enderecoRegistrador) {
    uint8_t resultado;
    uint16_t valor = 0;
    
    // Alterna para modo transmissão
    digitalWrite(PINO_RS485_DE_RE, HIGH);
    delayMicroseconds(100); // Pequeno delay para comutação
    
    // Leitura do registrador holding
    resultado = no.readHoldingRegisters(enderecoRegistrador, 1);
    
    // Alterna de volta para modo recepção
    digitalWrite(PINO_RS485_DE_RE, LOW);
    
    if (resultado == no.ku8MBSuccess) {
        valor = no.getResponseBuffer(0);
        return valor;
    } else {
        Serial.print("Erro Modbus no registrador 0x");
        Serial.print(enderecoRegistrador, HEX);
        Serial.print(": Código de erro ");
        Serial.println(resultado);
        return 0xFFFF; // Retorna valor de erro
    }
}

// Função para ler um registrador input de um escravo Modbus
uint16_t lerRegistradorInput(ModbusMaster &no, uint16_t enderecoRegistrador) {
    uint8_t resultado;
    uint16_t valor = 0;
    
    // Alterna para modo transmissão
    digitalWrite(PINO_RS485_DE_RE, HIGH);
    delayMicroseconds(100);
    
    // Leitura do registrador input
    resultado = no.readInputRegisters(enderecoRegistrador, 1);
    
    // Alterna de volta para modo recepção
    digitalWrite(PINO_RS485_DE_RE, LOW);
    
    if (resultado == no.ku8MBSuccess) {
        valor = no.getResponseBuffer(0);
        return valor;
    } else {
        Serial.print("Erro Modbus no registrador input 0x");
        Serial.print(enderecoRegistrador, HEX);
        Serial.print(": Código de erro ");
        Serial.println(resultado);
        return 0xFFFF;
    }
}

// ===== FUNÇÕES DE LEITURA DOS SENSORES =====
void lerSensores() {
    // Leitura do sensor NO2
    no2_ppm = lerConcentracaoNO2();
    
    // Leitura do sensor CO (Modbus)
    // Exemplo: Lendo do registrador holding 0x0000 (ajuste conforme necessário)
    valor_bruto_co = lerRegistradorModbus(sensorCO, REGISTRADOR_HOLDING_INICIO);
    
    // Leitura do sensor CO2 (Modbus)
    // Exemplo: Lendo do registrador input 0x0000 (ajuste conforme necessário)
    valor_bruto_co2 = lerRegistradorInput(sensorCO2, REGISTRADOR_INPUT_INICIO);
}

void imprimirDadosSensores() {
    Serial.println("\n===== LEITURAS DOS SENSORES =====");
    Serial.printf("NO2  : %.2f ppm (ADC: %d)\n", no2_ppm, analogRead(PINO_ANALOGICO_NO2));
    
    if (valor_bruto_co != 0xFFFF) {
        Serial.printf("CO   : %d (valor bruto do registrador 0x%04X)\n", valor_bruto_co, REGISTRADOR_HOLDING_INICIO);
    } else {
        Serial.println("CO   : Erro na leitura do sensor");
    }
    
    if (valor_bruto_co2 != 0xFFFF) {
        Serial.printf("CO2  : %d (valor bruto do registrador 0x%04X)\n", valor_bruto_co2, REGISTRADOR_INPUT_INICIO);
    } else {
        Serial.println("CO2  : Erro na leitura do sensor");
    }
    Serial.println("================================");
}

// ===== FUNÇÕES UTILITÁRIAS ADICIONAIS =====

// Função para ler múltiplos registradores de um escravo Modbus
void lerMultiplosRegistradores(ModbusMaster &no, uint16_t enderecoInicio, uint16_t quantidade, uint16_t *buffer) {
    uint8_t resultado;
    
    digitalWrite(PINO_RS485_DE_RE, HIGH);
    delayMicroseconds(100);
    
    resultado = no.readHoldingRegisters(enderecoInicio, quantidade);
    
    digitalWrite(PINO_RS485_DE_RE, LOW);
    
    if (resultado == no.ku8MBSuccess) {
        for (uint16_t i = 0; i < quantidade; i++) {
            buffer[i] = no.getResponseBuffer(i);
        }
    } else {
        Serial.printf("Erro ao ler múltiplos registradores: %d\n", resultado);
    }
}

// Função para escrever em um registrador Modbus
bool escreverRegistradorModbus(ModbusMaster &no, uint16_t enderecoRegistrador, uint16_t valor) {
    uint8_t resultado;
    
    digitalWrite(PINO_RS485_DE_RE, HIGH);
    delayMicroseconds(100);
    
    resultado = no.writeSingleRegister(enderecoRegistrador, valor);
    
    digitalWrite(PINO_RS485_DE_RE, LOW);
    
    return (resultado == no.ku8MBSuccess);
}

// Função para controle de direção RS485 (opcional - para uso com callbacks) Talvez essas funções não sirvam pra nada. Avaliar
void preTransmissao() {
    digitalWrite(PINO_RS485_DE_RE, HIGH);
}

void posTransmissao() {
    delayMicroseconds(100);
    digitalWrite(PINO_RS485_DE_RE, LOW);
}
