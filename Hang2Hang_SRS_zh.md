# Hang2Hang 课题需求规格说明书（SRS）

## 1. 文档说明

### 1.1 编写目的
本文档用于定义 Hang2Hang 课题的软件需求基线，统一开发、联调、测试与验收标准，确保系统在嵌入式平台上稳定实现“档案管理 + 查询统计 + 可视化展示 + 文件持久化”。

### 1.2 适用范围
适用于基于 GD32H7 + LVGL + FATFS 的野生动物档案管理系统，覆盖主界面、编辑界面、数据核心逻辑、SD 卡 CSV 读写、统计图表展示等需求。

### 1.3 目标读者
- 课题开发人员
- 测试与验收人员
- 指导老师/评审人员

---

## 2. 课题背景与总体目标

### 2.1 课题背景
项目需要在资源受限的嵌入式环境中实现可用、可维护的野生动物档案系统。由于课题采用双人并行开发（数据逻辑与界面交互分工实现），为降低联调成本、避免相互阻塞，需在立项阶段就明确 Core/UI 解耦边界与接口契约。

### 2.2 总体目标
1. 实现档案数据管理（查询、编辑、删除、保存、导出、加载）。
2. 提供可视化分析能力（KPI + 状态占比饼图）。
3. 构建 Core/UI 分层架构，提升可维护性与可测试性。

---

## 3. 设计需求

### 3.1 架构设计需求
系统应采用双模块平等架构：

1. Wildlife_Core（数据核心层）
- 负责 wildlife_record_t 数据库管理
- 负责过滤算法与统计计算
- 负责 FATFS 文件读写与 CSV 解析
- 禁止依赖 LVGL 或任何 UI 控件类型

2. Wildlife_UI（界面显示层）
- 负责 LVGL 布局、样式、事件与绘图
- 负责输入采集、结果展示、日志显示
- 禁止直接调用 FATFS 接口

3. 通信桥要求
- UI 通过 Core API 请求数据与操作
- Core 通过 UI 回调通知界面刷新与日志输出

### 3.2 运行环境需求
1. 硬件环境：GD32H7 系列 MCU，外接显示与 SD 卡。  
2. 软件环境：LVGL 图形库、FATFS 文件系统、中断与系统时钟正常。  
3. 存储约束：固定容量数组，记录上限 32 条。

### 3.3 数据与算法设计需求
1. 记录模型字段固定：name/category/image/level/area/quantity/status/updated_at/valid。  
2. 删除机制采用软删除（valid=0）。  
3. 名称关键字匹配需不区分大小写。  
4. 百分比统计需采用最大余数修正法，保证合计 100%。  
5. CSV 解析必须支持引号字段与转义双引号。

### 3.4 质量属性需求
1. 可维护性：模块边界清晰，接口稳定。  
2. 可测试性：Core 可脱离 LVGL 单测。  
3. 稳定性：SD 卡异常时系统可回退默认数据并提示。  
4. 实时性：查询/刷新过程应保持界面可响应。

---

## 4. 课题功能说明

### 4.1 功能总览
系统主要功能由以下子系统组成：

1. 档案管理功能
- 新增记录
- 修改记录
- 软删除记录

2. 查询分析功能
- 按类别/等级/区域模式筛选
- 关键字查询
- KPI 指标统计
- 状态占比饼图显示

3. 文件管理功能
- 启动加载数据库 CSV
- 手动保存数据库 CSV
- 导出当前过滤结果 CSV

4. 界面交互功能
- Main/Edit 双页签
- 表格与详情联动
- 图片预览浮层
- 操作日志显示
- 软键盘输入

### 4.2 功能需求清单（摘要）
1. FR-01：系统应支持默认数据初始化。  
2. FR-02：系统应支持模式 + 条件 + 关键字联合查询。  
3. FR-03：系统应支持编辑页新增/更新记录。  
4. FR-04：系统应支持软删除并自动刷新统计。  
5. FR-05：系统应支持保存全量有效记录到 CSV。  
6. FR-06：系统应支持导出过滤结果到 CSV。  
7. FR-07：系统应支持饼图占比绘制与文本标注。  
8. FR-08：系统应支持日志持续追加显示。

---

## 5. 外部接口需求

### 5.1 与 SD 卡 / FATFS 的交互
1. Core 层负责调用 FATFS（f_open/f_read/f_write/f_close）。  
2. 支持读取数据库文件并解析为内存记录。  
3. 支持写回数据库文件与导出文件。  
4. I/O 失败需返回错误码并通知 UI 记录日志。

### 5.2 与 LVGL 的交互
1. UI 层负责创建 Tab、Table、Dropdown、Textarea、Canvas、Image、Keyboard。  
2. 所有按钮与控件事件由 UI 层回调处理。  
3. UI 层通过 Core API 获取数据并刷新控件状态。

### 5.3 Core/UI 桥接接口（示例）
1. Core 对外接口
- core_apply_filter(mode, val, key)
- core_get_record(view_idx)
- core_get_stats()
- core_save_db()
- core_export_csv()
- core_update_record(view_idx, rec)
- core_delete_record(view_idx)
- core_load_db()

2. UI 回调接口
- ui_on_data_updated()
- ui_push_log(const char *msg)

---

## 6. 软件操作流程

### 6.1 启动流程
1. UI 初始化 LVGL 页面与控件。  
2. 调用 wildlife_core_boot(try_load_db=1)。  
3. Core 尝试从 SD 卡加载 CSV：
- 成功：更新内存数据库并通知 UI 刷新
- 失败：回退默认数据并通知 UI 记录日志

### 6.2 查询流程
1. 用户在 Main 页选择模式、过滤项并输入关键字。  
2. UI 调用 core_apply_filter(mode, val, key)。  
3. Core 生成过滤视图 + 统计结果。  
4. Core 调用 ui_on_data_updated + ui_push_log。  
5. UI 刷新表格、详情、KPI、饼图与报告区。

### 6.3 编辑流程
1. 用户在 Edit 页填写或修改表单。  
2. UI 执行输入校验（必填项、时间格式）。  
3. UI 调用 core_update_record(selected_view_idx, rec)。  
4. Core 写入记录、重建过滤结果并通知 UI。  
5. UI 刷新主界面并更新编辑提示。

### 6.4 删除流程
1. 用户选中记录并点击 Delete。  
2. UI 调用 core_delete_record(selected_view_idx)。  
3. Core 将 valid 置 0（软删除）并重算统计。  
4. UI 收到通知后刷新列表与图表。

### 6.5 保存与导出流程
1. 保存：UI 调用 core_save_db()，Core 写入全量有效记录。  
2. 导出：UI 调用 core_export_csv()，Core 写入当前过滤结果。  
3. Core 输出成功/失败日志，由 UI 显示。

---

## 7. 验收标准

1. 功能完整性
- 查询、编辑、删除、保存、导出、加载均可执行。

2. 数据一致性
- 表格、详情、KPI、饼图基于同一过滤结果。

3. 统计正确性
- 状态占比百分比总和为 100%。

4. 分层正确性
- Core 不依赖 LVGL。
- UI 不直接调用 FATFS。

5. 异常处理
- SD 卡读写失败时系统有明确日志反馈，且可继续运行。

---

## 8. 里程碑建议

1. M1：Core/UI 分层接口稳定。  
2. M2：核心功能回归通过（查询/编辑/保存/导出）。  
3. M3：异常路径测试通过（无卡/坏数据/写失败）。  
4. M4：形成可交付演示版本与答辩材料。

---

## 9. 数据格式约定

### 10.1 记录结构约定
系统统一使用 wildlife_record_t 作为档案实体，字段约定如下：

1. name：物种名称，字符串，最大 23 个可见字符，结尾必须有 '\0'。  
2. category：类别（如 Mammal/Bird），字符串，最大 19 个可见字符。  
3. image：图片路径，字符串，最大 47 个可见字符。  
4. level：保护等级，字符串，最大 19 个可见字符。  
5. area：分布区域，字符串，最大 23 个可见字符。  
6. quantity：数量，int32。  
7. status：状态，字符串，推荐枚举值：stable/recovering/decreasing/critical/extinct。  
8. updated_at：更新时间，固定格式字符串，最大 23 个可见字符。  
9. valid：记录有效标记，1 表示有效，0 表示软删除。

### 10.2 时间字段格式约定
updated_at 字段采用固定格式：

1. 格式：YY/MM/DD/HH/MM  
2. 固定长度：14  
3. 分隔符位置：2、5、8、11 必须是 '/'  
4. 缺省值：00/00/00/00/00

### 10.3 查询参数格式约定

1. mode：
- 0 = ByCategory
- 1 = ByLevel
- 2 = ByArea

2. val：过滤值，字符串。  
- ALL 表示不过滤。

3. key：名称关键字，字符串。  
- 空串表示不启用关键字过滤。  
- 匹配规则：不区分大小写子串匹配。

### 10.4 CSV 文件格式约定

1. 字段顺序固定：
name,category,image,level,area,quantity,status,updated_at

2. 行结束：支持 CRLF 或 LF。  
3. 引号规则：
- 字段可用双引号包裹。
- 字段中双引号采用两个双引号转义（""）。

4. 解析容错：
- 标题行可跳过。
- 空行跳过。
- 非法 quantity 行应判定为格式错误并跳过或返回错误码。

### 10.5 存储路径约定

1. 数据库文件：0:/wildlife_db.csv  
2. 导出文件：0:/wildlife_export.csv

### 10.6 统计数据格式约定

wildlife_stats_t 字段语义：

1. valid_count：有效记录总数。  
2. filtered_count：当前过滤命中数。  
3. critical_count：状态为 critical 或 extinct 的记录数。  
4. decreasing_count：状态为 decreasing 的记录数。  
5. total_quantity：当前过滤结果数量总和。  
6. status_percent[5]：五类状态整数百分比，必须满足总和 100（最大余数法修正）。


### 10.7 **通知方式约定（todo）**
---

## 10. 函数功能约定

### 11.1 设计原则

1. Core 层函数只做业务、数据与文件，不做 UI 渲染。  
2. UI 层函数只做交互与显示，不直接做 FATFS 读写。  
3. 输入参数必须做边界与空指针检查。  
4. 所有关键操作应产生日志文本并可回传 UI。

### 11.2 Core 层函数约定

1. wildlife_core_init()  
- 功能：初始化核心上下文与默认状态。  
- 输入：无。  
- 输出：无。  
- 约定：重置记录库、过滤状态、统计缓存。

2. wildlife_core_boot(uint8_t try_load_db)  
- 功能：系统启动装载入口。  
- 输入：是否尝试加载 SD 卡数据库。  
- 输出：wildlife_result_t。  
- 约定：可在加载失败时回退默认数据。

3. core_apply_filter(mode, val, key)  
- 功能：执行查询过滤并更新统计。  
- 输入：模式、过滤值、关键字。  
- 输出：wildlife_result_t。  
- 约定：完成后应触发 UI 数据更新通知。

4. core_get_view_count()  
- 功能：返回当前视图记录数。  
- 输入：无。  
- 输出：uint16。

5. core_get_record(view_idx)  
- 功能：按视图索引取记录。  
- 输入：视图索引。  
- 输出：记录只读指针，越界返回 NULL。

6. core_get_stats()  
- 功能：返回当前统计快照。  
- 输入：无。  
- 输出：wildlife_stats_t。

7. core_get_filter_options(out, out_size)  
- 功能：返回当前模式下过滤选项字符串。  
- 输入：输出缓冲区及大小。  
- 输出：wildlife_result_t。  
- 约定：字符串格式为 LVGL 下拉框多行选项格式。

8. core_update_record(view_idx, rec)  
- 功能：更新或新增记录。  
- 输入：视图索引、记录结构。  
- 输出：wildlife_result_t。  
- 约定：
- view_idx 有效时执行更新。
- view_idx 无效时执行新增（找空槽）。

9. core_delete_record(view_idx)  
- 功能：软删除记录。  
- 输入：视图索引。  
- 输出：wildlife_result_t。  
- 约定：仅设置 valid=0，不做物理删除。

10. core_save_db() / core_export_csv() / core_load_db()  
- 功能：数据库保存、导出、加载。  
- 输入：无。  
- 输出：wildlife_result_t。  
- 约定：
- 只能在 Core 内调用 FATFS。
- 文件操作结果必须通过日志回传。

### 11.3 UI 层函数约定

1. wildlife_ui_init()  
- 功能：创建页面、控件和事件绑定。  
- 输入：无。  
- 输出：无。

2. ui_on_data_updated()  
- 功能：Core 通知后的统一刷新入口。  
- 输入：无。  
- 输出：无。  
- 约定：应统一刷新表格、详情、KPI、饼图、报告区。

3. ui_push_log(const char *msg)  
- 功能：追加显示日志文本。  
- 输入：日志字符串。  
- 输出：无。  
- 约定：处理缓冲区上限，避免越界。

4. UI 事件回调函数（如 Query/Apply/Delete）  
- 功能：收集输入、做基本校验、调用 Core API。  
- 约定：
- 禁止直接操作 Core 内部数组。
- 禁止直接调用 FATFS。

### 11.4 错误码与返回约定

wildlife_result_t 语义建议：

1. WL_OK：成功。  
2. WL_ERR_PARAM：参数非法。  
3. WL_ERR_NOT_FOUND：目标不存在。  
4. WL_ERR_IO：文件读写失败。  
5. WL_ERR_FORMAT：数据格式错误。

约定：

1. Core 关键函数必须返回上述错误码之一。  
2. UI 收到非 WL_OK 时应给出明确提示并写日志。
