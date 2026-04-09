#!/usr/bin/env python3
"""
Video Stream Monitor - 主监控脚本
实时监控多路 UVC 摄像头视频流状态
"""

import cv2
import yaml
import time
import argparse
import logging
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

# 配置日志
LOG_DIR = Path(__file__).parent / "logs"
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(LOG_DIR / "monitor.log"),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class CameraMonitor:
    """摄像头监控器"""
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        self.cameras: Dict[str, dict] = {}
        self.frames_analyzed = 0
        self.last_check_time: Dict[str, datetime] = {}
        
    def load_config(self) -> List[dict]:
        """加载摄像头配置"""
        with open(self.config_path, 'r') as f:
            config = yaml.safe_load(f)
        return config.get('streams', [])
    
    def start_monitors(self) -> bool:
        """启动所有摄像头监控"""
        streams = self.load_config()
        success_count = 0
        
        for stream in streams:
            name = stream['name']
            source = stream['source']
            
            cap = cv2.VideoCapture(source)
            
            if not cap.isOpened():
                logger.error(f"❌ 无法打开摄像头：{source} ({name})")
                self.cameras[name] = {
                    'source': source,
                    'cap': None,
                    'status': 'offline',
                    'resolution': stream.get('resolution', 'unknown'),
                    'fps': stream.get('fps', 30)
                }
                continue
            
            # 设置摄像头参数
            cap.set(cv2.CAP_PROP_FPS, stream.get('fps', 30))
            
            self.cameras[name] = {
                'source': source,
                'cap': cap,
                'status': 'running',
                'resolution': stream.get('resolution', 'unknown'),
                'fps': stream.get('fps', 30)
            }
            
            logger.info(f"✅ 摄像头启动成功：{name} ({source})")
            success_count += 1
        
        return success_count > 0
    
    def check_all_cameras(self) -> Dict:
        """检查所有摄像头状态"""
        report = {
            'timestamp': datetime.now().isoformat(),
            'cameras': {},
            'summary': {
                'total': len(self.cameras),
                'running': 0,
                'offline': 0
            }
        }
        
        for name, info in self.cameras.items():
            if info['cap'] is None:
                info['status'] = 'offline'
                report['summary']['offline'] += 1
                report['cameras'][name] = {
                    'status': 'offline',
                    'error': '无法打开设备'
                }
                continue
            
            ret, frame = info['cap'].read()
            
            if ret:
                info['status'] = 'running'
                info['last_frame_time'] = datetime.now()
                self.frames_analyzed += 1
                report['summary']['running'] += 1
                report['cameras'][name] = {
                    'status': 'running',
                    'resolution': info['resolution'],
                    'fps': info['fps']
                }
            else:
                info['status'] = 'disconnected'
                report['summary']['offline'] += 1
                report['cameras'][name] = {
                    'status': 'disconnected',
                    'error': '读取帧失败'
                }
        
        return report
    
    def continuous_monitor(self, interval: int = 5):
        """连续监控模式"""
        logger.info("🔄 启动连续监控模式...")
        
        while True:
            try:
                report = self.check_all_cameras()
                
                # 打印状态
                status_line = f"【{report['timestamp'][:19]}】"
                for name, info in report['cameras'].items():
                    status = info['status'].upper()
                    status_line += f" {name}: {status}"
                
                logger.info(status_line)
                
                # 记录异常
                for name, info in report['cameras'].items():
                    if info['status'] != 'running':
                        logger.warning(f"⚠️  摄像头异常：{name} - {info.get('error', '未知错误')}")
                
                time.sleep(interval)
                
            except KeyboardInterrupt:
                logger.info("👋 监控停止")
                break
            except Exception as e:
                logger.error(f"❌ 监控错误：{e}")
                time.sleep(5)
    
    def one_shot_check(self):
        """单次检查模式"""
        report = self.check_all_cameras()
        
        print("\n" + "="*60)
        print(f"📹 摄像头健康报告 - {report['timestamp']}")
        print("="*60)
        print(f"总数：{report['summary']['total']} | 运行：{report['summary']['running']} | 离线：{report['summary']['offline']}")
        print("-"*60)
        
        for name, info in report['cameras'].items():
            status = info['status'].upper()
            icon = "✅" if info['status'] == 'running' else "❌"
            print(f"{icon} {name}: {status}")
            if 'error' in info:
                print(f"   错误：{info['error']}")
        
        print("="*60 + "\n")
        
        return report
    
    def stop_all(self):
        """关闭所有摄像头"""
        for name, info in self.cameras.items():
            if info['cap'] is not None:
                info['cap'].release()
                logger.info(f"🔒 关闭摄像头：{name}")


def main():
    parser = argparse.ArgumentParser(description='视频流监控器')
    parser.add_argument('--config', '-c', default='config/streams.yaml', help='配置文件路径')
    parser.add_argument('--mode', '-m', choices=['continuous', 'one-shot'], default='one-shot', help='运行模式')
    parser.add_argument('--interval', '-i', type=int, default=5, help='连续监控间隔（秒）')
    
    args = parser.parse_args()
    
    logger.info("🚀 启动视频流监控器...")
    
    monitor = CameraMonitor(args.config)
    
    if not monitor.start_monitors():
        logger.error("❌ 没有摄像头启动成功，退出")
        return 1
    
    try:
        if args.mode == 'continuous':
            monitor.continuous_monitor(interval=args.interval)
        else:
            monitor.one_shot_check()
    finally:
        monitor.stop_all()
    
    return 0


if __name__ == "__main__":
    exit(main())
