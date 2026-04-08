#!/usr/bin/env python3
"""
Frame Capture Script - 定时抓拍脚本
定时从摄像头采集单帧图片并保存
"""

import cv2
import yaml
import time
import argparse
import logging
from datetime import datetime
from pathlib import Path

# 配置日志
LOG_DIR = Path(__file__).parent / "logs"
SNAPSHOTS_DIR = LOG_DIR / "snapshots"
SNAPSHOTS_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(LOG_DIR / "capture.log"),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class FrameCapturer:
    """帧抓拍器"""
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        self.streams = self.load_config()
        self.running = True
        
    def load_config(self) -> list:
        """加载配置"""
        with open(self.config_path, 'r') as f:
            config = yaml.safe_load(f)
        return config.get('streams', [])
    
    def capture_frame(self, camera_info: dict) -> Optional[str]:
        """从指定摄像头抓拍一帧"""
        name = camera_info['name']
        source = camera_info['source']
        
        cap = cv2.VideoCapture(source)
        
        if not cap.isOpened():
            logger.error(f"❌ 无法打开摄像头：{source}")
            return None
        
        # 等待摄像头初始化
        time.sleep(0.5)
        
        ret, frame = cap.read()
        
        if not ret:
            logger.error(f"❌ 读取帧失败：{name}")
            cap.release()
            return None
        
        # 生成文件名
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{name}_{timestamp}.jpg"
        filepath = SNAPSHOTS_DIR / filename
        
        # 保存图片
        cv2.imwrite(str(filepath), frame, [cv2.IMWRITE_JPEG_QUALITY, 90])
        
        logger.info(f"✅ 抓拍成功：{filename}")
        
        cap.release()
        return str(filepath)
    
    def capture_all(self):
        """抓拍所有摄像头"""
        logger.info("📸 开始抓拍所有摄像头...")
        
        saved_files = []
        
        for stream in self.streams:
            filepath = self.capture_frame(stream)
            if filepath:
                saved_files.append(filepath)
        
        return saved_files
    
    def run_loop(self, interval: int = 300):
        """循环抓拍模式"""
        logger.info(f"🔄 启动循环抓拍模式，间隔 {interval} 秒")
        
        while self.running:
            try:
                files = self.capture_all()
                
                if files:
                    logger.info(f"💾 已保存 {len(files)} 张图片")
                
                # 等待下一次抓拍
                for i in range(interval):
                    if not self.running:
                        break
                    time.sleep(1)
                    if i % 60 == 0:
                        logger.info(f"⏳ 距离下次抓拍还有 {interval - i} 秒")
            
            except KeyboardInterrupt:
                logger.info("👋 抓拍停止")
                break
            except Exception as e:
                logger.error(f"❌ 抓拍错误：{e}")
                time.sleep(10)
    
    def stop(self):
        """停止抓拍"""
        self.running = False


def main():
    parser = argparse.ArgumentParser(description='定时抓拍脚本')
    parser.add_argument('--config', '-c', default='config/streams.yaml', help='配置文件路径')
    parser.add_argument('--once', '-o', action='store_true', help='只抓拍一次')
    parser.add_argument('--interval', '-i', type=int, default=300, help='循环抓拍间隔（秒），默认 5 分钟')
    parser.add_argument('--count', '-n', type=int, default=None, help='抓拍次数，None 为无限')
    
    args = parser.parse_args()
    
    logger.info("🚀 启动帧抓拍器...")
    
    capturer = FrameCapturer(args.config)
    
    try:
        if args.once:
            # 单次抓拍
            files = capturer.capture_all()
            if files:
                logger.info(f"✅ 完成！保存了 {len(files)} 张图片")
            else:
                logger.warning("⚠️  没有图片被保存")
        else:
            # 循环抓拍
            count = 0
            while capturer.running:
                capturer.run_loop(interval=args.interval)
                if args.count and count >= args.count:
                    break
                count += 1
                if args.count:
                    logger.info(f"📊 已完成 {count}/{args.count} 次抓拍")
    
    finally:
        capturer.stop()
        logger.info("🔒 抓拍器已停止")
    
    return 0


if __name__ == "__main__":
    exit(main())
