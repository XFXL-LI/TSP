#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将空气微站/TSP 项目 SD 卡历史 .dat 数据还原为 Excel。

适用固件数据结构：
    #pragma pack(push, 1)
    struct fileStorage {
        uint64_t timestamp;   // YYYYMMDDHHMMSS 或统计窗口时间
        char sensor_id[15];   // HJ212/项目因子编码，C 字符串
        float value;          // 实时值或统计平均值
        float min_val;        // 最小值
        float max_val;        // 最大值
        uint8_t is_valid;     // 1=有效，0=无效
    };
    #pragma pack(pop)

目录约定：
    /sdcard/YYYYMMDD/raw/HH/MM.dat   实时数据，type=0
    /sdcard/YYYYMMDD/min/HH/MM.dat   分钟数据，type=1
    /sdcard/YYYYMMDD/hour/HH.dat     小时数据，type=2
    /sdcard/YYYYMMDD/day/day.dat     日数据，type=3

用法示例：
    # 导出全部历史数据
    python tools/export_sd_history_to_excel.py D:\\sdcard_backup -o output\\history_all.xlsx --mode all

    # 只导出某一天
    python tools/export_sd_history_to_excel.py D:\\sdcard_backup -o output\\history_20260709.xlsx --mode day --day 20260709

    # 只导出日期范围，包含起止日期
    python tools/export_sd_history_to_excel.py D:\\sdcard_backup -o output\\history_20260701_20260709.xlsx --mode range --date-from 20260701 --date-to 20260709

    # 输入也可以是某个 raw/min/hour/day 目录或单个 .dat 文件
    python tools/export_sd_history_to_excel.py D:\\sdcard_backup\\20260709\\raw -o output\\raw_20260709.xlsx --type raw

依赖：
    pip install openpyxl
当前 Codex/项目环境通常已包含 openpyxl。
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import struct
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


RECORD_STRUCT = struct.Struct("<Q15sfffB")
RECORD_SIZE = RECORD_STRUCT.size  # 36 bytes

DATA_TYPE_BY_DIR = {
    "raw": 0,
    "min": 1,
    "hour": 2,
    "day": 3,
}

DATA_TYPE_NAME = {
    0: "raw",
    1: "min",
    2: "hour",
    3: "day",
}

DATA_TYPE_CN = {
    0: "2011",
    1: "2051",
    2: "2061",
    3: "2031",
}

# 可按现场配置继续补充。脚本不会依赖这个映射；没有映射时仍输出 sensor_id。
DEFAULT_SENSOR_NAMES = {
    "a01001": "温度",
    "a01002": "湿度",
    "a01006": "大气压",
    "a01007": "风速",
    "a01008": "风向",
    "a34001": "TSP",
    "a34002": "PM10",
    "a34004": "PM2.5",
    "a34005": "PM1",
    "a05024": "CO",
    "a21026": "SO2",
    "a21004": "NO2",
    "a05024": "CO",
    "a05029": "O3",
    "a24088": "TVOC",
    "LA": "噪声LA",
    "L90": "噪声L90",
}


@dataclass
class HistoryRecord:
    source_file: str
    data_type: int
    data_type_name: str
    cn: str
    timestamp: int
    datetime_text: str
    date: str
    hour: str
    minute: str
    second: str
    sensor_id: str
    sensor_name: str
    value: float
    min_val: float
    max_val: float
    is_valid: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="将空气微站/TSP SD卡历史 .dat 二进制数据导出为 Excel/CSV。",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "input",
        help="SD卡备份根目录、某个日期目录、raw/min/hour/day目录，或单个 .dat 文件。",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="output/history_export.xlsx",
        help="输出 Excel 文件路径；如果没有 openpyxl，会自动输出同名 CSV。",
    )
    parser.add_argument(
        "--type",
        choices=["auto", "raw", "min", "hour", "day"],
        default="auto",
        help="当输入路径无法从目录名识别数据类型时，手动指定。",
    )
    parser.add_argument(
        "--mode",
        choices=["all", "day", "range"],
        default="all",
        help="导出模式：all=全部；day=指定某一天；range=指定日期范围。",
    )
    parser.add_argument(
        "--day",
        help="--mode day 时使用，格式 YYYYMMDD。",
    )
    parser.add_argument(
        "--date-from",
        help="--mode range 时使用，开始日期，格式 YYYYMMDD，包含该日。",
    )
    parser.add_argument(
        "--date-to",
        help="--mode range 时使用，结束日期，格式 YYYYMMDD，包含该日。",
    )
    parser.add_argument(
        "--sensor-map",
        help="可选 CSV 映射文件，列为 sensor_id,sensor_name，用于替换中文名称。",
    )
    parser.add_argument(
        "--csv-only",
        action="store_true",
        help="只输出 CSV，不生成 Excel。",
    )
    parser.add_argument(
        "--no-wide",
        action="store_true",
        help="不生成按时间展开的宽表 sheet，仅输出明细。",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="遇到文件长度不是 36 字节整数倍时报错；默认会跳过尾部残缺字节。",
    )
    args = parser.parse_args()
    normalize_mode_args(parser, args)
    return args


def validate_yyyymmdd(parser: argparse.ArgumentParser, value: Optional[str], name: str) -> Optional[str]:
    if value is None:
        return None
    if not re.fullmatch(r"\d{8}", value):
        parser.error(f"{name} 必须是 YYYYMMDD 格式，例如 20260709。")
    try:
        datetime.strptime(value, "%Y%m%d")
    except ValueError:
        parser.error(f"{name} 不是有效日期：{value}")
    return value


def normalize_mode_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """把 all/day/range 转换成内部使用的 date_from/date_to。"""
    args.day = validate_yyyymmdd(parser, args.day, "--day")
    args.date_from = validate_yyyymmdd(parser, args.date_from, "--date-from")
    args.date_to = validate_yyyymmdd(parser, args.date_to, "--date-to")

    if args.mode == "all":
        if args.day or args.date_from or args.date_to:
            print(
                "[WARN] --mode all 会忽略 --day/--date-from/--date-to，导出全部数据。",
                file=sys.stderr,
            )
        args.date_from = None
        args.date_to = None
        return

    if args.mode == "day":
        if not args.day:
            parser.error("--mode day 必须指定 --day YYYYMMDD。")
        args.date_from = args.day
        args.date_to = args.day
        return

    if args.mode == "range":
        if not args.date_from or not args.date_to:
            parser.error("--mode range 必须同时指定 --date-from YYYYMMDD 和 --date-to YYYYMMDD。")
        if args.date_from > args.date_to:
            parser.error("--date-from 不能晚于 --date-to。")
        return


def load_sensor_names(path: Optional[str]) -> Dict[str, str]:
    names = dict(DEFAULT_SENSOR_NAMES)
    if not path:
        return names
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            sid = (row.get("sensor_id") or "").strip()
            name = (row.get("sensor_name") or "").strip()
            if sid and name:
                names[sid] = name
    return names


def infer_type_from_path(path: Path, forced: str = "auto") -> Optional[int]:
    if forced != "auto":
        return DATA_TYPE_BY_DIR[forced]
    parts = [p.lower() for p in path.parts]
    for name, typ in DATA_TYPE_BY_DIR.items():
        if name in parts:
            return typ
    return None


def iter_dat_files(input_path: Path) -> Iterable[Path]:
    if input_path.is_file():
        if input_path.suffix.lower() == ".dat":
            yield input_path
        return
    for p in input_path.rglob("*.dat"):
        if p.is_file():
            yield p


def parse_timestamp(ts: int) -> Tuple[str, str, str, str, str]:
    text = str(ts)
    # 固件通常写 YYYYMMDDHHMMSS，小时/日统计可能尾部为 0000/000000。
    if len(text) < 8:
        return text, "", "", "", ""
    text = text.zfill(14)
    year = text[0:4]
    month = text[4:6]
    day = text[6:8]
    hour = text[8:10]
    minute = text[10:12]
    second = text[12:14]
    date = f"{year}{month}{day}"
    dt_text = f"{year}-{month}-{day} {hour}:{minute}:{second}"
    try:
        datetime.strptime(dt_text, "%Y-%m-%d %H:%M:%S")
    except ValueError:
        # 保留原格式，避免因为异常时间导致整条记录丢失。
        dt_text = text
    return dt_text, date, hour, minute, second


def date_in_range(date: str, start: Optional[str], end: Optional[str]) -> bool:
    if start and date < start:
        return False
    if end and date > end:
        return False
    return True


def clean_sensor_id(raw: bytes) -> str:
    raw = raw.split(b"\x00", 1)[0]
    return raw.decode("ascii", errors="ignore").strip()


def read_dat_file(
    path: Path,
    data_type: int,
    sensor_names: Dict[str, str],
    date_from: Optional[str],
    date_to: Optional[str],
    strict: bool,
    root: Path,
) -> List[HistoryRecord]:
    data = path.read_bytes()
    if len(data) % RECORD_SIZE != 0:
        msg = (
            f"{path} 文件长度 {len(data)} 不是记录长度 {RECORD_SIZE} 的整数倍，"
            f"尾部残缺 {len(data) % RECORD_SIZE} 字节。"
        )
        if strict:
            raise ValueError(msg)
        print("[WARN]", msg, file=sys.stderr)

    records: List[HistoryRecord] = []
    count = len(data) // RECORD_SIZE
    rel = str(path.relative_to(root)) if path.is_relative_to(root) else str(path)
    for idx in range(count):
        chunk = data[idx * RECORD_SIZE : (idx + 1) * RECORD_SIZE]
        timestamp, sensor_raw, value, min_val, max_val, is_valid = RECORD_STRUCT.unpack(chunk)
        dt_text, date, hour, minute, second = parse_timestamp(timestamp)
        if date and not date_in_range(date, date_from, date_to):
            continue
        sensor_id = clean_sensor_id(sensor_raw)
        records.append(
            HistoryRecord(
                source_file=rel,
                data_type=data_type,
                data_type_name=DATA_TYPE_NAME[data_type],
                cn=DATA_TYPE_CN[data_type],
                timestamp=timestamp,
                datetime_text=dt_text,
                date=date,
                hour=hour,
                minute=minute,
                second=second,
                sensor_id=sensor_id,
                sensor_name=sensor_names.get(sensor_id, sensor_id),
                value=float(value),
                min_val=float(min_val),
                max_val=float(max_val),
                is_valid=int(is_valid),
            )
        )
    return records


def collect_records(args: argparse.Namespace) -> List[HistoryRecord]:
    input_path = Path(args.input).expanduser().resolve()
    if not input_path.exists():
        raise FileNotFoundError(f"输入路径不存在：{input_path}")

    sensor_names = load_sensor_names(args.sensor_map)
    files = sorted(iter_dat_files(input_path))
    if not files:
        raise FileNotFoundError(f"未找到 .dat 文件：{input_path}")

    root = input_path if input_path.is_dir() else input_path.parent
    all_records: List[HistoryRecord] = []
    skipped = 0
    for file in files:
        data_type = infer_type_from_path(file, args.type)
        if data_type is None:
            print(
                f"[WARN] 无法从路径识别数据类型，已跳过：{file}；"
                "可用 --type raw/min/hour/day 手动指定。",
                file=sys.stderr,
            )
            skipped += 1
            continue
        all_records.extend(
            read_dat_file(
                file,
                data_type,
                sensor_names,
                args.date_from,
                args.date_to,
                args.strict,
                root,
            )
        )

    all_records.sort(key=lambda r: (r.timestamp, r.data_type, r.sensor_id, r.source_file))
    print(f"[INFO] dat文件数：{len(files)}，跳过：{skipped}，记录数：{len(all_records)}")
    return all_records


def record_to_row(r: HistoryRecord) -> List[object]:
    return [
        r.datetime_text,
        r.timestamp,
        r.date,
        r.hour,
        r.minute,
        r.second,
        r.data_type_name,
        r.data_type,
        r.cn,
        r.sensor_id,
        r.sensor_name,
        r.value,
        r.min_val,
        r.max_val,
        r.is_valid,
        r.source_file,
    ]


HEADERS = [
    "datetime",
    "timestamp",
    "date",
    "hour",
    "minute",
    "second",
    "data_type",
    "data_type_code",
    "hj212_cn",
    "sensor_id",
    "sensor_name",
    "value",
    "min_val",
    "max_val",
    "is_valid",
    "source_file",
]


def write_csv(records: List[HistoryRecord], output: Path) -> Path:
    csv_path = output.with_suffix(".csv")
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with open(csv_path, "w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(HEADERS)
        for r in records:
            writer.writerow(record_to_row(r))
    return csv_path


def make_wide_rows(records: List[HistoryRecord], field: str = "value") -> Tuple[List[str], List[List[object]]]:
    sensors = sorted({r.sensor_id for r in records})
    grouped: Dict[Tuple[int, str, str], Dict[str, object]] = {}
    meta: Dict[Tuple[int, str, str], HistoryRecord] = {}
    for r in records:
        key = (r.timestamp, r.data_type_name, r.cn)
        grouped.setdefault(key, {})[r.sensor_id] = getattr(r, field)
        meta.setdefault(key, r)
    headers = ["datetime", "timestamp", "data_type", "hj212_cn"] + sensors
    rows: List[List[object]] = []
    for key in sorted(grouped.keys()):
        r = meta[key]
        values = grouped[key]
        rows.append([r.datetime_text, r.timestamp, r.data_type_name, r.cn] + [values.get(s) for s in sensors])
    return headers, rows


def write_excel(records: List[HistoryRecord], output: Path, no_wide: bool) -> Path:
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Alignment, Font, PatternFill
        from openpyxl.utils import get_column_letter
    except ImportError:
        print("[WARN] 未安装 openpyxl，改为输出 CSV。", file=sys.stderr)
        return write_csv(records, output)

    output.parent.mkdir(parents=True, exist_ok=True)
    wb = Workbook()
    ws = wb.active
    ws.title = "all_records"
    ws.append(HEADERS)
    for r in records:
        ws.append(record_to_row(r))

    # 按类型分 sheet，方便直接筛选。
    for typ in ["raw", "min", "hour", "day"]:
        rows = [r for r in records if r.data_type_name == typ]
        if not rows:
            continue
        sheet = wb.create_sheet(typ)
        sheet.append(HEADERS)
        for r in rows:
            sheet.append(record_to_row(r))

    if not no_wide:
        for field in ["value", "min_val", "max_val"]:
            headers, rows = make_wide_rows(records, field)
            if rows:
                sheet = wb.create_sheet(f"wide_{field}")
                sheet.append(headers)
                for row in rows:
                    sheet.append(row)

    # 汇总 sheet
    summary = wb.create_sheet("summary", 0)
    summary.append(["项目", "值"])
    summary.append(["记录总数", len(records)])
    summary.append(["开始时间", records[0].datetime_text if records else ""])
    summary.append(["结束时间", records[-1].datetime_text if records else ""])
    summary.append(["传感器数量", len({r.sensor_id for r in records})])
    summary.append(["数据类型", ", ".join(sorted({r.data_type_name for r in records}))])
    summary.append(["说明", f"二进制记录长度 {RECORD_SIZE} 字节，解析格式 <Q15sfffB。"])

    for sheet in wb.worksheets:
        sheet.freeze_panes = "A2"
        sheet.auto_filter.ref = sheet.dimensions
        for cell in sheet[1]:
            cell.font = Font(bold=True, color="FFFFFF")
            cell.fill = PatternFill("solid", fgColor="1F4E78")
            cell.alignment = Alignment(horizontal="center", vertical="center")
        for col_idx, col in enumerate(sheet.columns, 1):
            max_len = 8
            for cell in col[:2000]:
                val = "" if cell.value is None else str(cell.value)
                max_len = max(max_len, min(len(val), 45))
            sheet.column_dimensions[get_column_letter(col_idx)].width = max_len + 2

    wb.save(output)
    return output


def main() -> int:
    args = parse_args()
    records = collect_records(args)
    output = Path(args.output).expanduser().resolve()
    if args.csv_only:
        written = write_csv(records, output)
    else:
        written = write_excel(records, output, args.no_wide)
    print(f"[OK] 已导出：{written}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
