#!/usr/bin/env python3
"""
Health Check Script - 健康检查脚本
检查摄像头健康状态并生成报告
"""

import cv2
import yaml
import json
import argparse
import logging
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List

# 配置日志
LOG_DIR = Path(__file__).parent / "logs"
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler(LOG_DIR / "health.log"),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


class HealthChecker:
    """健康检查器"""
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        self.streams = self.load_config()
        self.health_history = []
        
    def load_config(self) -> list:
        """加载配置"""
        with open(self.config_path, 'r') as f:
            config = yaml.safe_load(f)
        return config.get('streams', [])
    
    def check_camera(self, camera_info: dict) -> dict:
        """检查单个摄像头健康状态"""
        name = camera_info['name']
        source = camera_info['source']
        expected_fps = camera_info.get('fps', 30)
        expected_resolution = camera_info.get('resolution', '1920x1080')
        
        result = {
            'camera': name,
            'source': source,
            'timestamp': datetime.now().isoformat(),
            'status': 'unknown',
            'details': {}
        }
        
        try:
            cap = cv2.VideoCapture(source)
            
            if not cap.isOpened():
                result['status'] = 'offline'
                result['details']['error'] = '设备无法打开'
                logger.warning(f"⚠️  {name}: {result['details']['error']}")
                return result
            
            # 获取实际参数
            actual_fps = cap.get(cv2.CAP_PROP_FPS)
            actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            
            result['details']['expected_fps'] = expected_fps
            result['details']['actual_fps'] = actual_fps
            result['details']['expected_resolution'] = expected_resolution
            result['details']['actual_resolution'] = f"{actual_width}x{actual_height}"
            
            # 测试读取帧
            ret, frame = cap.read()
            
            if not ret:
                result['status'] = 'error'
                result['details']['error'] = '无法读取帧'
                logger.error(f"❌ {name}: {result['details']['error']}")
                cap.release()
                return result
            
            # 检查帧质量
            if frame is None or frame.size == 0:
                result['status'] = 'error'
                result['details']['error'] = '帧为空'
                logger.error(f"❌ {name}: {result['details']['error']}")
                cap.release()
                return result
            
            result['status'] = 'healthy'
            result['details']['message'] = '运行正常'
            logger.info(f"✅ {name}: {actual_width}x{actual_height} @ {actual_fps:.1f}fps")
            
            cap.release()
            
        except Exception as e:
            result['status'] = 'error'
            result['details']['error'] = str(e)
            logger.error(f"❌ {name}: {e}")
        
        return result
    
    def check_all(self) -> dict:
        """检查所有摄像头"""
        logger.info("🏥 开始健康检查...")
        
        results = []
        summary = {
            'total': len(self.streams),
            'healthy': 0,
            'offline': 0,
            'error': 0,
            'timestamp': datetime.now().isoformat()
        }
        
        for stream in self.streams:
            result = self.check_camera(stream)
            results.append(result)
            
            if result['status'] == 'healthy':
                summary['healthy'] += 1
            elif result['status'] == 'offline':
                summary['offline'] += 1
            else:
                summary['error'] += 1
        
        summary['report'] = results
        
        return summary
    
    def generate_report(self, summary: dict, report_type: str = 'json'):
        """生成报告"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        if report_type == 'json':
            filepath = LOG_DIR / f"health_{timestamp}.json"
            with open(filepath, 'w') as f:
                json.dump(summary, f, indent=2, ensure_ascii=False)
            logger.info(f"📄 报告已保存：{filepath}")
            return str(filepath)
        
        elif report_type == 'text':
            filepath = LOG_DIR / f"health_{timestamp}.txt"
            
            with open(filepath, 'w') as f:
                f.write("="*60 + "\n")
                f.write(f"📊 摄像头健康报告 - {summary['timestamp']}\n")
                f.write("="*60 + "\n\n")
                
                f.write(f"📈 统计信息:\n")
                f.write(f"  总数：{summary['total']}\n")
                f.write(f"  健康：{summary['healthy']}\n")
                f.write(f"  离线：{summary['offline']}\n")
                f.write(f"  错误：{summary['error']}\n\n")
                
                f.write("-"*60 + "\n")
                f.write(f"📹 详细状态:\n")
                f.write("-"*60 + "\n\n")
                
                for result in summary['report']:
                    status_icon = "✅" if result['status'] == 'healthy' else "❌"
                    f.write(f"{status_icon} {result['camera']}: {result['status'].upper()}\n")
                    
                    if 'details' in result:
                        details = result['details']
                        if 'actual_resolution' in details:
                            f.write(f"   分辨率：{details['actual_resolution']} (期望：{details.get('expected_resolution', 'N/A')})\n")
                        if 'actual_fps' in details:
                            f.write(f"   FPS: {details['actual_fps']:.1f} (期望：{details.get('expected_fps', 'N/A')})\n")
                        if 'error' in details:
                            f.write(f"   错误：{details['error']}\n")
                    f.write("\n")
                
                f.write("="*60 + "\n")
            
            logger.info(f"📄 报告已保存：{filepath}")
            return str(filepath)
    
    def get_summary_text(self, summary: dict) -> str:
        """获取摘要文本（用于推送通知）"""
        total = summary['total']
        healthy = summary['healthy']
        offline = summary['offline']
        error = summary['error']
        
        if offline > 0 or error > 0:
            status = "⚠️  有摄像头异常"
            details = f"\n健康：{healthy}/{total}, 离线：{offline}, 错误：{error}"
        else:
            status = "✅ 所有摄像头正常"
            details = ""
        
        return f"{status}{details}\n时间：{summary['timestamp']}"


def main():
    parser = argparse.ArgumentParser(description='摄像头健康检查脚本')
    parser.add_argument('--config', '-c', default='config/streams.yaml', help='配置文件路径')
    parser.add_argument('--format', '-f', choices=['json', 'text'], default='text', help='报告格式')
    parser.add_argument('--no-report', action='store_true', help='只检查不生成报告')
    
    args = parser.parse_args()
    
    logger.info("🚀 启动健康检查...")
    
    checker = HealthChecker(args.config)
    summary = checker.check_all()
    
    # 打印摘要
    print("\n" + checker.get_summary_text(summary) + "\n")
    
    # 生成报告
    if not args.no_report:
        filepath = checker.generate_report(summary, args.format)
        print(f"📄 报告已保存：{filepath}\n")
    
    # 返回状态码
    if summary['healthy'] == summary['total']:
        return 0
    else:
        return 1


if __name__ == "__main__":
    exit(main())
