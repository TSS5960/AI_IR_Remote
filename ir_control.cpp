#include "ir_control.h"

IRsend irsend(IR_TX_PIN);
IRrecv irrecv(IR_RX_PIN);
decode_results results;

IRDaikinESP daikinAC(IR_TX_PIN);
IRMitsubishiAC mitsubishiAC(IR_TX_PIN);
IRPanasonicAc panasonicAC(IR_TX_PIN);
IRGreeAC greeAC(IR_TX_PIN);
IRMideaAC mideaAC(IR_TX_PIN);
IRHaierAC haierAC(IR_TX_PIN);
IRSamsungAc samsungAC(IR_TX_PIN);
IRLgAc lgAC(IR_TX_PIN);
IRFujitsuAC fujitsuAC(IR_TX_PIN);
IRHitachiAc hitachiAC(IR_TX_PIN);

void initIR() {
  irsend.begin();
  irrecv.enableIRIn();
  
  daikinAC.begin();
  mitsubishiAC.begin();
  panasonicAC.begin();
  greeAC.begin();
  mideaAC.begin();
  haierAC.begin();
  samsungAC.begin();
  lgAC.begin();
  fujitsuAC.begin();
  hitachiAC.begin();
  
  Serial.printf("红外发射器初始化完成 (GPIO%d)\n", IR_TX_PIN);
  Serial.printf("红外接收器初始化完成 (GPIO%d)\n", IR_RX_PIN);
  Serial.println("支持10个空调品牌");
}

uint8_t convertMode(ACMode mode) {
  switch(mode) {
    case MODE_AUTO: return 0;
    case MODE_COOL: return 1;
    case MODE_HEAT: return 2;
    case MODE_DRY:  return 3;
    case MODE_FAN:  return 4;
    default: return 1;
  }
}

uint8_t convertFanSpeed(FanSpeed speed) {
  switch(speed) {
    case FAN_AUTO: return 0;
    case FAN_LOW:  return 1;
    case FAN_MED:  return 2;
    case FAN_HIGH: return 3;
    default: return 0;
  }
}

void sendACState(const ACState& state) {
  Serial.println("\n[IR] 发送空调状态...");
  Serial.printf("     品牌: %s\n", getBrandName(state.brand));
  Serial.printf("     电源: %s\n", state.power ? "开" : "关");
  Serial.printf("     温度: %dC\n", state.temperature);
  
  switch(state.brand) {
    case BRAND_DAIKIN:
      daikinAC.setPower(state.power);
      daikinAC.setTemp(state.temperature);
      daikinAC.setMode(convertMode(state.mode));
      daikinAC.setFan(convertFanSpeed(state.fanSpeed));
      daikinAC.send();
      Serial.println("     -> Daikin信号已发送");
      break;
      
    case BRAND_MITSUBISHI:
      mitsubishiAC.setPower(state.power);
      mitsubishiAC.setTemp(state.temperature);
      mitsubishiAC.setMode(convertMode(state.mode));
      mitsubishiAC.setFan(convertFanSpeed(state.fanSpeed));
      mitsubishiAC.send();
      Serial.println("     -> Mitsubishi信号已发送");
      break;
      
    case BRAND_PANASONIC:
      panasonicAC.setPower(state.power);
      panasonicAC.setTemp(state.temperature);
      panasonicAC.setMode(convertMode(state.mode));
      panasonicAC.setFan(convertFanSpeed(state.fanSpeed));
      panasonicAC.send();
      Serial.println("     -> Panasonic信号已发送");
      break;
      
    case BRAND_GREE:
      greeAC.setPower(state.power);
      greeAC.setTemp(state.temperature);
      greeAC.setMode(convertMode(state.mode));
      greeAC.setFan(convertFanSpeed(state.fanSpeed));
      greeAC.send();
      Serial.println("     -> Gree信号已发送");
      break;
      
    case BRAND_MIDEA:
      mideaAC.setPower(state.power);
      mideaAC.setTemp(state.temperature);
      mideaAC.setMode(convertMode(state.mode));
      mideaAC.setFan(convertFanSpeed(state.fanSpeed));
      mideaAC.send();
      Serial.println("     -> Midea信号已发送");
      break;
      
    case BRAND_HAIER:
      haierAC.setCommand(state.power ? kHaierAcCmdOn : kHaierAcCmdOff);
      haierAC.setTemp(state.temperature);
      haierAC.setMode(convertMode(state.mode));
      haierAC.setFan(convertFanSpeed(state.fanSpeed));
      haierAC.send();
      Serial.println("     -> Haier信号已发送");
      break;
      
    case BRAND_SAMSUNG:
      samsungAC.setPower(state.power);
      samsungAC.setTemp(state.temperature);
      samsungAC.setMode(convertMode(state.mode));
      samsungAC.setFan(convertFanSpeed(state.fanSpeed));
      samsungAC.send();
      Serial.println("     -> Samsung信号已发送");
      break;
      
    case BRAND_LG:
      lgAC.setPower(state.power);
      lgAC.setTemp(state.temperature);
      lgAC.setMode(convertMode(state.mode));
      lgAC.setFan(convertFanSpeed(state.fanSpeed));
      lgAC.send();
      Serial.println("     -> LG信号已发送");
      break;
      
    case BRAND_FUJITSU:
      fujitsuAC.setPower(state.power);
      fujitsuAC.setTemp(state.temperature);
      fujitsuAC.setMode(convertMode(state.mode));
      fujitsuAC.setFanSpeed(convertFanSpeed(state.fanSpeed));
      fujitsuAC.send();
      Serial.println("     -> Fujitsu信号已发送");
      break;
      
    case BRAND_HITACHI:
      hitachiAC.setPower(state.power);
      hitachiAC.setTemp(state.temperature);
      hitachiAC.setMode(convertMode(state.mode));
      hitachiAC.setFan(convertFanSpeed(state.fanSpeed));
      hitachiAC.send();
      Serial.println("     -> Hitachi信号已发送");
      break;
      
    default:
      Serial.println("     WARN: 未知品牌，使用Daikin默认");
      daikinAC.setPower(state.power);
      daikinAC.setTemp(state.temperature);
      daikinAC.send();
      break;
  }
  
  delay(100);
}

void initIRReceiver() {
  irrecv.enableIRIn();
  Serial.println("红外接收器已启动");
}

void startLearnMode() {
  irrecv.enableIRIn();
  Serial.println("\n========================================");
  Serial.println("  进入学习模式");
  Serial.println("========================================");
  Serial.println("请对准接收器按下遥控器按键...");
  Serial.println("按 'q' 退出学习模式");
  Serial.println("========================================\n");
}

void stopLearnMode() {
  Serial.println("\n退出学习模式\n");
}

bool checkReceivedSignal() {
  if (irrecv.decode(&results)) {
    printReceivedCode(&results);
    irrecv.resume();
    return true;
  }
  return false;
}

void printReceivedCode(decode_results* results) {
  Serial.println("\n========================================");
  Serial.println("  接收到红外信号！");
  Serial.println("========================================");
  Serial.print("协议: ");
  Serial.println(typeToString(results->decode_type));
  Serial.print("代码: 0x");
  serialPrintUint64(results->value, HEX);
  Serial.println();
  Serial.print("位数: ");
  Serial.println(results->bits);
  Serial.println("========================================\n");
}
