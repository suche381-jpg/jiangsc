# Hang2Hang_annotation.c 模块化重构设计文档

## 1. 目标与约束

### 1.1 重构目标
将单体实现拆分为两个完全独立、平等协作的模块：

- 模块 A：数据核心层 Wildlife_Core  
负责数据模型、数据库状态、FATFS 文件读写、CSV 解析、查询过滤、KPI 统计、饼图百分比修正。
- 模块 B：界面显示层 Wildlife_UI  
负责 LVGL 页面布局、样式、事件回调、画布渲染、软键盘、页面切换与交互反馈。

### 1.2 强约束
- Wildlife_Core 严禁包含 lvgl.h，严禁使用任何 lv_obj_t 指针。
- Wildlife_UI 不直接调用 FATFS 接口（f_open、f_read、f_write、f_unlink 等）。
- 双方只通过定义良好的 Bridge API 通信，不共享对方内部状态结构。
- 功能等价保留：软删除、CSV 引号字段解析、最大余数百分比修正、双页签切换、日志反馈、详情联动。

---

## 2. 单体代码问题分析（现状）

原单体文件同时维护以下职责，耦合度高：

- 数据状态：记录库、过滤索引、选择索引、文件浏览状态、日志缓存。
- 业务逻辑：CSV 读写、过滤算法、统计计算、导出路径生成、状态映射。
- UI 逻辑：LVGL 控件创建、样式、事件分发、Tab 切换、Canvas 绘图、软键盘。
- IO 与显示混杂：按钮回调内直接做文件操作和数据写入，再直接刷新控件。

主要风险：
- 代码不可复用（难以替换 UI 或替换数据源）。
- 业务测试困难（需要 UI 环境才能走核心逻辑）。
- 维护复杂（任何字段变更可能影响 UI 和 IO 多条链路）。

---

## 3. 模块拆分方案

## 3.1 模块 A：Wildlife_Core（纯逻辑层）

职责边界：
- wildlife_record_t 数据库（固定容量数组）维护。
- 过滤视图索引管理（view_idx、view_count）。
- 查询模式字段选择（category/level/area）。
- 关键字不区分大小写匹配。
- KPI 统计与饼图百分比修正（最大余数法）。
- CSV 解析（支持引号和双引号转义）与 FATFS 读写。
- 软删除（valid 置 0）。
- 核心日志文本生成（不负责具体显示）。

不做的事：
- 不创建任何 LVGL 对象。
- 不直接刷新表格、标签、画布。
- 不关心控件布局与样式。

## 3.2 模块 B：Wildlife_UI（纯显示层）

职责边界：
- 组织 Main/Edit 双页签 UI。
- 维护控件句柄与样式。
- 采集用户输入并调用 Core API。
- 将 Core 返回数据渲染为表格、详情、KPI、饼图。
- 处理图片预览层、软键盘、交互提示与日志显示。

不做的事：
- 不访问 FATFS。
- 不直接改写 Core 内部数据库数组。
- 不实现过滤算法和 CSV 解析。

---

## 4. 通信桥梁（Bridge API）设计

## 4.1 共享定义文件
建议在 [jiangsuchecks/mycks/Wildlife_Def.h](jiangsuchecks/mycks/Wildlife_Def.h) 仅保留：

- 常量：容量、状态桶数量、默认时间字符串。
- 枚举：wildlife_mode_t、wildlife_result_t。
- 数据结构：wildlife_record_t、wildlife_stats_t。

说明：此文件不承载模块行为函数，保持“纯契约数据”。

## 4.2 Core 对 UI 暴露接口（UI 调用）
建议在 [jiangsuchecks/mycks/Wildlife_Core.h](jiangsuchecks/mycks/Wildlife_Core.h)：

- 初始化与启动
  - wildlife_core_init()
  - wildlife_core_boot(try_load_db)

- 查询链路
  - core_apply_filter(mode, val, key)
  - core_get_view_count()
  - core_get_record(view_idx)
  - core_get_filter_options(out, out_size)

- 统计与消息
  - core_get_stats()
  - core_get_last_message()

- 数据维护
  - core_update_record(view_idx, rec)
  - core_delete_record(view_idx)

- 文件操作
  - core_load_db()
  - core_save_db()
  - core_export_csv()

## 4.3 UI 回调接口（Core 通知 UI）
建议在 [jiangsuchecks/mycks/Wildlife_UI.h](jiangsuchecks/mycks/Wildlife_UI.h)：

- ui_on_data_updated
  - Core 完成加载/过滤/编辑后触发，UI 统一刷新表格、详情、仪表盘、饼图。
- ui_push_log
  - Core 发送状态文本，UI 负责追加展示。

---

## 5. 关键交互时序

## 5.1 查询操作流（Query）

1. UI 读取控件输入：mode、filter、name_key。  
2. UI 调用 core_apply_filter(mode, filter, key)。  
3. Core 内部执行：
- 重建可选过滤项；
- 扫描有效记录并生成视图索引；
- 计算 KPI 与状态百分比（最大余数法）；
- 记录日志文本。  
4. Core 调用 ui_push_log / ui_on_data_updated。  
5. UI 在刷新函数中调用：
- core_get_view_count + core_get_record 填表；
- core_get_stats 刷新 KPI 和饼图；
- core_get_last_message 刷新报告区。

## 5.2 编辑操作流（Edit）

1. UI 收集编辑表单，做输入完整性与时间格式校验。  
2. UI 组装 wildlife_record_t。  
3. UI 调用 core_update_record(selected_view_idx, rec)。  
4. Core 执行：
- 若有选中行则覆盖更新；否则找空槽新增；
- 维护 valid 标志；
- 重新过滤与统计；
- 产生日志并通知 UI。  
5. UI 收到数据变化通知后统一刷新，并更新编辑提示文字。  

删除流类似：
- UI 调用 core_delete_record(selected_view_idx)
- Core 软删除 valid=0，随后重算并通知 UI。

---

## 6. 算法与实现要点

## 6.1 过滤算法
- 三条件叠加：
  - 模式字段匹配（category/level/area）
  - 下拉项匹配（ALL 放行）
  - 名称关键字不区分大小写匹配

## 6.2 CSV 解析
- 支持字段外层双引号。
- 支持双引号转义（两个双引号代表一个双引号）。
- 兼容 CRLF/LF 行结束。

## 6.3 百分比修正（最大余数法）
- 先按整数除法得到初始百分比。
- 记录各项余数。
- 将剩余点数按余数从大到小补齐，直到总和 100%。

## 6.4 软删除
- 不物理移除记录，仅 valid=0。
- 新增优先复用空槽位，保持固定数组模型。

---

## 7. 建议文件结构

- [jiangsuchecks/mycks/Wildlife_Def.h](jiangsuchecks/mycks/Wildlife_Def.h)  
共享枚举与结构体（无行为函数）。
- [jiangsuchecks/mycks/Wildlife_Core.h](jiangsuchecks/mycks/Wildlife_Core.h)  
Core API 声明。
- [jiangsuchecks/mycks/Wildlife_Core.c](jiangsuchecks/mycks/Wildlife_Core.c)  
Core 逻辑与 FATFS/CSV 实现。
- [jiangsuchecks/mycks/Wildlife_UI.h](jiangsuchecks/mycks/Wildlife_UI.h)  
UI API 与 Core->UI 回调声明。
- [jiangsuchecks/mycks/Wildlife_UI.c](jiangsuchecks/mycks/Wildlife_UI.c)  
UI 布局、事件、绘图、软键盘。

---

## 8. 重构验收标准

满足以下条件即判定重构成功：

- Core 编译不依赖 lvgl.h。
- UI 文件中不存在 FATFS 直接调用。
- 所有查询/编辑/保存/导出/加载都通过 Core API。
- 软删除、CSV 引号解析、百分比修正、双页签切换仍可用。
- UI 崩溃不会破坏核心数据库状态，Core 逻辑可独立单测。

---

## 9. 迁移建议（渐进式）

1. 先抽共享结构到 Wildlife_Def.h。  
2. 将过滤、统计、CSV 和文件写入优先迁入 Wildlife_Core.c。  
3. UI 回调逐个改为调用 Core API。  
4. 最后移除单体文件中的剩余业务逻辑，保留仅 UI 绑定层。  
5. 增加最小回归用例：查询、编辑、删除、保存、导出、加载。

---

## 10. 结论

该拆分方案可实现逻辑与显示彻底分离，模块互为平等协作关系：  
- Core 作为唯一业务真相源，负责数据与规则。  
- UI 作为纯渲染与交互外壳，负责展示与输入。  
- 通过稳定 Bridge API 连接，既可持续迭代，也可未来替换任一层实现而不影响另一层。
