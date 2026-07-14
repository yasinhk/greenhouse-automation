/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"  
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
// FIX: DMA arka planda günceller — volatile ZORUNLU
volatile uint32_t adc_degerleri[4];
uint8_t fan_acik = 0;
uint8_t pompa_acik = 0;
float adc_value;
float ortalama;
float yuzdeortalama;
float suseviye;
char  json_buffer[150];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// --- PIN AYARLARI ---
#define DHT_PORT           GPIOA
#define DHT_PIN            GPIO_PIN_5

#define FAN_PORT           GPIOB
#define FAN_PIN            GPIO_PIN_12
#define FAN_ACMA_SICAKLIGI 25.0f

#define POMPA_ROLE_PORT      GPIOB
#define POMPA_ROLE_PIN       GPIO_PIN_13
#define POMPA_ACMA_SEVIYESI  35.0f

// --- DHT22 DEGISKENLERI ---
uint8_t  Rh_byte1, Rh_byte2, Temp_byte1, Temp_byte2;
uint8_t  SUM;              // FIX: uint16_t'den uint8_t'e degistirildi (checksum 1 byte'tir)
uint16_t RH, TEMP;
volatile float   Temperature = 0.0f;
volatile float   Humidity    = 0.0f;
volatile uint8_t Presence    = 0;

// --- FONKSIYONLAR ---

// 24 MHz için yaklasik gecikme (DWT yok, basit döngü)
// --- YENI VE KUSURSUZ ZAMANLAYICI ---
void delay_us(uint16_t us)
{
    __HAL_TIM_SET_COUNTER(&htim2, 0);  // Kronometreyi sifirla
    while (__HAL_TIM_GET_COUNTER(&htim2) < us); // Istenen mikrosaniye dolana kadar bekle
}

void Set_Pin_Output(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

void Set_Pin_Input(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL; // Harici 10k direncimiz oldugu için Pull-Up kapali
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

void DHT22_Start(void)
{
    Set_Pin_Output(DHT_PORT, DHT_PIN);
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, 0); // Hatti 0'a çek
    delay_us(1200); // 1.2 milisaniye bekle (CT Makalesi Standardi)
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, 1); // Hatti serbest birak
    delay_us(20);   // 20 mikrosaniye bekle ve dinlemeye geç
    Set_Pin_Input(DHT_PORT, DHT_PIN);
}

uint8_t DHT22_Check_Response(void)
{
    uint8_t Response = 0;
    delay_us(40); // Sensörün hatti 0'a çekmesini bekle
    if (!(HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN)))
    {
        delay_us(80); // Sensörün hatti 1'e çekmesini bekle
        if ((HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN))) Response = 1;
        else Response = -1;
    }
    // Sensör cevap verdi, hattin tekrar 0 olmasini (verinin baslamasini) bekle
    while ((HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN))); 
    return Response;
}

uint8_t DHT22_Read(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        // Pin HIGH olana kadar bekle
        while (!(HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN))); 
        
        delay_us(40); // Sirri burada: 40us bekle. Eger hala HIGH ise deger '1'dir, LOW ise '0'dir.
        
        if (!(HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN))) // Eger LOW olmussa
        {
            i &= ~(1 << (7 - j)); // Biti 0 yaz
        }
        else // Eger hala HIGH ise
        {
            i |= (1 << (7 - j));  // Biti 1 yaz
            while ((HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN))); // Pinin tekrar LOW olmasini bekle
        }
    }
    return i;
}

void JSON_Gonder(void)
{
    // 1. Röle durumlarini Active-Low mantigiyla oku
    uint8_t fan_durum   = HAL_GPIO_ReadPin(FAN_PORT, FAN_PIN) == GPIO_PIN_RESET ? 1 : 0;
    uint8_t pompa_durum = HAL_GPIO_ReadPin(POMPA_ROLE_PORT, POMPA_ROLE_PIN) == GPIO_PIN_RESET ? 1 : 0;

    // 2. Ondalikli Sayilari (Float) Tam Sayi Parçalarina Ayirma Algoritmasi
    // Örnek: Sicaklik 24.5 ise -> temp_tam = 24, temp_ondalik = 5 olur.
    
    // Sicaklik
    int temp_tam = (int)Temperature;
    int temp_ondalik = (int)((Temperature - temp_tam) * 10.0f);
    if(temp_ondalik < 0) temp_ondalik = -temp_ondalik; // Negatif sicaklik çakismasini önler

    // Nem
    int hum_tam = (int)Humidity;
    int hum_ondalik = (int)((Humidity - hum_tam) * 10.0f);
    if(hum_ondalik < 0) hum_ondalik = -hum_ondalik;

    // Toprak Nemi
    int soil_tam = (int)yuzdeortalama;
    int soil_ondalik = (int)((yuzdeortalama - soil_tam) * 10.0f);
    if(soil_ondalik < 0) soil_ondalik = -soil_ondalik;

    // Su Seviyesi
    int water_tam = (int)suseviye;
    int water_ondalik = (int)((suseviye - water_tam) * 10.0f);
    if(water_ondalik < 0) water_ondalik = -water_ondalik;

    // 3. Verileri "%d.%d" mantigiyla JSON içine güvenle yerlestir
    snprintf(json_buffer, sizeof(json_buffer),
        "{\"temp\":%d.%d,\"hum\":%d.%d,\"soil\":%d.%d,\"water\":%d.%d,\"fan\":%d,\"pump\":%d}\n",
        temp_tam, temp_ondalik, 
        hum_tam, hum_ondalik, 
        soil_tam, soil_ondalik, 
        water_tam, water_ondalik, 
        fan_durum, 
        pompa_durum
    );

    // 4. Hazirlanan JSON paketini ESP32'ye firlat
    HAL_UART_Transmit(&huart1, (uint8_t*)json_buffer, strlen(json_buffer), 100);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
    // FIX: STM32F1 ADC'nin dogru çalismasi için kalibrasyon DMA baslatmadan ÖNCE yapilmali
		HAL_TIM_Base_Start(&htim2); // kronometreyi baslat
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_degerleri, 4);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	HAL_Delay(1000);
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

// ---------------------------------------------------------
        // 1. ADIM: SENSÖR VERILERINI HESAPLA VE KALIBRE ET
        // ---------------------------------------------------------
        ortalama = (adc_degerleri[0] + adc_degerleri[1] + adc_degerleri[2]) / 3.0f;
        suseviye = (adc_degerleri[3] * 100.0f) / 4095.0f; 

        // KENDI SENSÖRÜNÜN KALIBRASYON DEGERLERI (Watch ekranindan aldiklarin)
        float HAVADA_ADC = 2450.0f; // Kuru durum (Maksimum ADC degeri)
        float SUDA_ADC   = 880.0f; // Islak durum (Minimum ADC degeri)

        // Dogrusal Haritalama (Map) Formülü (Ters Oranti)
        yuzdeortalama = ((ortalama - HAVADA_ADC) * (100.0f - 0.0f)) / (SUDA_ADC - HAVADA_ADC) + 0.0f;

        // Güvenlik Kilidi (Constraint) - Degerlerin %0'in altina veya %100'ün üstüne çikmasini engeller
        if (yuzdeortalama < 0.0f) {
            yuzdeortalama = 0.0f;
        }
        else if (yuzdeortalama > 100.0f) {
            yuzdeortalama = 100.0f;
        }
				// SU SEVIYESI KALIBRASYONU (Dogru Oranti) ---
        // Önce mevcut formülle ham yüzdeyi aliyoruz
        float ham_su_yuzdesi = (adc_degerleri[3] * 100.0f) / 4095.0f; 
        
        float HAVADA_SU_SINIR = 6.86f;  // Depo tamamen bosken okunan ham deger (%0 Su)
        float SUDA_SU_SINIR   = 34.5f;  // Depo tamamen doluyken okunan ham deger (%100 Su)

        // Dogru orantili haritalama formülü
        suseviye = ((ham_su_yuzdesi - HAVADA_SU_SINIR) * 100.0f) / (SUDA_SU_SINIR - HAVADA_SU_SINIR);

        // Su seviyesi sinir kilidi
        if (suseviye < 0.0f)   suseviye = 0.0f;
        if (suseviye > 100.0f) suseviye = 100.0f;

				// ---------------------------------------------------------
        // 2. ADIM: POMPA KONTROLÜ (KUSURSUZ ÇALISAN MANTIK)
        // ---------------------------------------------------------
        
        // ÖNCE GÜVENLIK: Su seviyesi 30'un altindaysa toprak ne olursa olsun pompayi kapat!
        if (suseviye < 30.0f) 
        {
            pompa_acik = 0;
            HAL_GPIO_WritePin(POMPA_ROLE_PORT, POMPA_ROLE_PIN, GPIO_PIN_SET); // Pompayi KAPAT (Active-Low: SET)
        }
        else 
        {
            // EGER SU VARSA (30'dan büyükse) senin istedigin sartlar çalisabilir:
            if (!pompa_acik && yuzdeortalama <= 40.0f)
            {
                pompa_acik = 1;
                HAL_GPIO_WritePin(POMPA_ROLE_PORT, POMPA_ROLE_PIN, GPIO_PIN_RESET); // Pompayi AÇ (Active-Low: RESET)
            }
            else if (pompa_acik && yuzdeortalama >= 60.0f)
            {
                pompa_acik = 0;
                HAL_GPIO_WritePin(POMPA_ROLE_PORT, POMPA_ROLE_PIN, GPIO_PIN_SET);   // Pompayi KAPAT (Active-Low: SET)
            }
        }

// ---------------------------------------------------------
        // 3. ADIM: DHT22 SENSÖRÜ (KESME KORUMALI)
        // ---------------------------------------------------------
        
        // Islemcinin arka plan islerini dondur, pür dikkat sensörü dinle
        __disable_irq(); 
        
        DHT22_Start();
        Presence = DHT22_Check_Response();

        if (Presence == 1)
        {
            Rh_byte1   = DHT22_Read();
            Rh_byte2   = DHT22_Read();
            Temp_byte1 = DHT22_Read();
            Temp_byte2 = DHT22_Read();
            SUM        = DHT22_Read();
        }
        
        // Sensörle isimiz bitti, arka plan islerini ve zamanlayicilari geri aç
        __enable_irq(); 

        // Veri hesaplamalarini kesmeler açikken (güvenli bölgede) yap
        if (Presence == 1)
        {
            if (SUM == ((Rh_byte1 + Rh_byte2 + Temp_byte1 + Temp_byte2) & 0xFF))
            {
                TEMP = ((Temp_byte1 & 0x7F) << 8) | Temp_byte2;
                Temperature = (float)TEMP / 10.0f;

                if (Temp_byte1 & 0x80)
                {
                    Temperature = -Temperature;
                }

                RH       = (Rh_byte1 << 8) | Rh_byte2;
                Humidity = (float)RH / 10.0f;
            }
        }

        // ---------------------------------------------------------
        // 4. ADIM: FAN KONTROLÜ (Active-Low)
        // ---------------------------------------------------------
				if (!fan_acik && Temperature >= 21.0f)
				{
				fan_acik = 1;
				HAL_GPIO_WritePin(FAN_PORT, FAN_PIN, GPIO_PIN_RESET);
				}
				else if (fan_acik && Temperature < 21.0f)
				{
				fan_acik = 0;
				HAL_GPIO_WritePin(FAN_PORT, FAN_PIN, GPIO_PIN_SET);
				}

        // ---------------------------------------------------------
        // 5. ADIM: VERILERI GÖNDER VE BEKLE
        // ---------------------------------------------------------
        JSON_Gonder();
        HAL_Delay(2000);
			}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 4;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 24-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOB, FAN_ROLE_Pin|POMPA_ROLE_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : DHT_PIN_Pin */
  GPIO_InitStruct.Pin = DHT_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DHT_PIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : FAN_ROLE_Pin POMPA_ROLE_Pin */
  GPIO_InitStruct.Pin = FAN_ROLE_Pin|POMPA_ROLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
