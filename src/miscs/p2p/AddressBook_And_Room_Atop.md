# 设备通讯录 & 自建房间 接口文档

> 版本:v1.0(2026-07-07)
> 模块:moss 通讯录域(NFC 加好友 / 自建房间加好友 / 好友申请审批)

## 1. 概述

为智能玩具等 IoT 设备提供"通讯录 + 加好友"能力,支持两条加好友途径:

- **NFC 碰一碰**:设备贴近对方设备,发起好友申请;
- **自建房间**:房主创建临时房间获得 4 位房间码(默认 2 分钟有效),口头告知朋友,朋友在设备或 App 上输码进房,向房内成员批量发好友申请。

好友申请统一走**双侧管理员审批**:申请落库后,双方各自的"管理员"(设备侧 = 设备所属家庭管理员;用户侧 = 用户本人)在 App 审批,**双侧都同意后互写通讯录**,设备即可对好友发起 RTC 呼叫。

接口分两组:

| 组 | 协议 | 调用方 | 登录态 |
|---|---|---|---|
| 设备 API | ATOP DOT(`thing.ai.contact.*`) | 设备端 | 免登录,设备身份由网关注入 |
| 端 API | Fusion RESTFUL(`/v1.0/m/life/ai/contact/...`) | App | 需 C 端用户登录 |

### 1.1 公共枚举

**成员/联系人类型**(全链路统一,对齐 leona RtcResourceType):

| 值 | 含义 |
|---|---|
| 0 | 设备(id = devId) |
| 1 | App 用户(id = uid) |
| 2 | sip(预留,暂未启用) |

**审批状态**:`PENDING`(待审)/ `APPROVED`(已同意)/ `REJECTED`(已拒绝)。任一侧 REJECTED 即申请完结(失败);双侧 APPROVED 即申请完结(成功,互写通讯录);完结后同一对身份可重新发起申请。

**通讯录来源 source**:0 = 家庭同步(设备初始化全量同步,可被重建);1 = 好友申请(审批通过写入,不会被家庭同步删除)。

**目标成员标识格式**:批量发申请的 `targets` 每项为 `"{memberType}:{memberId}"`,如 `"0:vdevo153xxxx"`(设备)、`"1:ay16289xxxx"`(用户)。

### 1.2 限制与频控

| 项 | 默认值 | 说明 |
|---|---|---|
| 房间存活时长 | 120s | Apollo `contact.room.ttl.seconds`,到期自动销毁,码回收复用 |
| 房间成员上限 | 20 | Apollo `contact.room.member.limit` |
| 输错码频控 | 5 次/分钟 | Apollo `contact.room.join.fail.limit`;对不存在的码的加入/查询/发申请均计数,超限拉黑 1 分钟 |

---

## 2. 设备 API(ATOP DOT)

设备身份(devId)由网关上下文注入,不可伪造,请求无需传自身设备 ID。

### 2.1 thing.ai.contact.sync — 设备初始化同步通讯录

设备开机初始化时调用。全量重建本设备通讯录:拉取所属家庭下所有**具备 RTC 能力**的设备(`rtc_capability` 元数据)与所有家庭成员写入通讯录(source=0);已通过好友申请写入的联系人(source=1)不受影响。

- 入参:无
- 返回:`ContactListItem[]`(见 4.1)

### 2.2 thing.ai.contact.apply — NFC 扫码添加好友

设备 NFC 碰一碰后调用,向对方设备发起好友申请(设备↔设备)。已是好友或已有在途申请则幂等返回,不重复创建。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| targetDevId | body | String | 是 | 对方设备 ID |

- 返回:无(成功即申请已创建,等待双侧管理员审批)

### 2.3 thing.ai.contact.list — 查询通讯录列表

返回本设备全部启用状态的联系人(含家庭同步与好友来源)。

- 入参:无
- 返回:`ContactListItem[]`(见 4.1)

### 2.3.1 thing.ai.contact.delete — 删除好友联系人

双向删除**好友申请来源**(source=1)的联系人:本方通讯录删除对方,对方为设备时其通讯录中的本设备记录也一并删除;删除后双方可重新发起好友申请。**家庭同步来源(source=0)的联系人不允许删除**(返回 `13890605`),家庭联系人由家庭成员/设备关系决定。本方通讯录无该记录时幂等成功。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| contactType | body | Integer | 是 | 联系人类型:0=设备 1=用户 |
| targetId | body | String | 是 | 目标标识:设备 devId 或用户 uid |

- 返回:
  - 成功(含重复删,幂等):`{"success": true, "t": 1783590000000}`
  - 删除家庭原生联系人:`{"success": false, "errorCode": "13890605", "errorMsg": "family contact cannot be deleted", "t": 1783590000000}`
  - 参数错误(`contactType` 不是 0/1、`targetId` 为空)或删除失败:`{"success": false, "errorCode": "13890012", "errorMsg": "params illegal", "t": 1783590000000}`

### 2.4 thing.ai.contact.room.create — 创建通讯录房间

创建临时房间,云端生成一个当前未被占用的 4 位数字码。创建者自动成为首个成员(房主)。

- 入参:无
- 返回:`Room`(见 4.2)

### 2.5 thing.ai.contact.room.join — 输码加入房间

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |

- 返回:`Room`(含加入后的最新成员列表)
- 码不存在或房间已过期返回 `13890601`;满员返回 `13890602`;输错码超频返回 `13890017`。重复加入幂等。

### 2.6 thing.ai.contact.room.members — 查询房间成员列表

端上轮询此接口感知新成员进房(拉模式,无推送)。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |

- 返回:`Room`

### 2.7 thing.ai.contact.room.apply — 向房间成员发好友申请

向房内指定成员批量发申请。全选成员即"一键全发",传子集即"勾选发送"。发起者必须在房内。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |
| targets | body | List\<String> | 是 | 目标成员列表,每项 `memberType:memberId` |

- 返回:`Integer` — 实际创建的申请数
- 自动跳过(不计入返回值):自己、不在房内的目标、用户↔用户组合(不支持)、已是好友、已有在途申请。

### 2.8 thing.ai.contact.room.leave — 退出房间

**房主退出 = 解散房间**(所有人的房间立即失效);普通成员退出只移除自己。房间已过期时幂等成功。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |

- 返回:无

---

## 3. 端 API(Fusion RESTFUL,App)

需 C 端用户登录。房间类接口支持**双身份**:请求带 `devId` 即"替自家设备操作"(需为该设备所属家庭成员,否则 `13890013`),成员身份为该设备;不带 `devId` 即以登录用户本人身份(memberType=1)。

### 3.1 POST /v1.0/m/life/ai/contact/room/create — 创建房间

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| devId | body | String | 否 | 以设备身份操作时传设备 ID |

- 返回:`Room`

### 3.2 POST /v1.0/m/life/ai/contact/room/join — 输码加入房间

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |
| devId | body | String | 否 | 以设备身份操作时传设备 ID |

- 返回:`Room`;错误码同 2.5

### 3.3 GET /v1.0/m/life/ai/contact/room/members — 查询房间成员

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | query | String | 是 | 4 位房间码 |

- 返回:`Room`

### 3.4 POST /v1.0/m/life/ai/contact/room/apply — 向房间成员发好友申请

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |
| targets | body | List\<String> | 是 | 目标成员列表,每项 `memberType:memberId` |
| devId | body | String | 否 | 以设备身份发起时传设备 ID |

- 返回:`Integer` — 实际创建的申请数;跳过规则同 2.7

### 3.5 POST /v1.0/m/life/ai/contact/room/leave — 退出房间

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| code | body | String | 是 | 4 位房间码 |
| devId | body | String | 否 | 以设备身份操作时传设备 ID |

- 返回:无;房主退出即解散

### 3.6 GET /v1.0/m/life/ai/contact/application/list — 查询待审批好友申请

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| devId | query | String | 否 | 不传:查"我本人作为被申请方"的待审;传:校验当前用户为该设备**家庭管理员**后,查该设备相关(作为申请方或被申请方)的待审 |

- 返回:`Application[]`(见 4.3),按申请对去重、创建时间倒序
- 非该设备家庭管理员返回 `13890013`

### 3.7 POST /v1.0/m/life/ai/contact/application/review — 审批好友申请

审批者身份自动推断:当前用户有权代表申请方侧(申请方是设备且用户为其家庭管理员 / 申请方是用户本人)则审申请方侧;否则审被申请方侧。**双侧都 APPROVED 后系统自动互写双方通讯录**(source=1)。

| 参数 | 位置 | 类型 | 必填 | 说明 |
|---|---|---|---|---|
| applicationId | body | String | 是 | 申请业务主键(来自 3.6 列表) |
| approve | body | Boolean | 是 | true=同意 false=拒绝 |

- 返回:无
- 语义说明:
  - 对两侧均无权限 → `13890013`;
  - 申请不存在 → `13890604`;
  - 已被拒绝的申请再审 / 已双侧通过的申请再"拒绝" → `13890012`(已完结);
  - 已双侧通过的申请再"同意" → 幂等成功(内部补偿重跑通讯录写入,可用于修复写入半途失败);
  - 并发审批安全:同侧重复审批幂等,不会互相覆盖。

---

## 4. 数据结构

### 4.1 ContactListItem(通讯录条目)

```json
{
  "resourceId": "vdevo153xxxx",   // 设备 devId 或用户 uid
  "resourceName": "小明的玩具",     // 显示名
  "icon": "https://.../avatar.png",
  "resourceType": 0,               // 0=设备 1=用户
  "rtcCodeValue": 3                // RTC 能力值,仅设备类型有效,用户恒 0
}
```

### 4.2 Room(房间)

```json
{
  "code": "8642",                  // 4 位房间码
  "expireAt": 1783407368538,       // 过期时间戳(毫秒),到期房间自动销毁
  "members": [
    {
      "memberType": 0,             // 0=设备 1=用户
      "memberId": "vdevo153xxxx",
      "name": "小明的玩具",
      "avatar": "https://.../a.png"
    }
  ]
}
```

### 4.3 Application(好友申请)

```json
{
  "applicationId": "2074386466636644352",
  "applicantType": 0,              // 申请方类型:0=设备 1=用户
  "applicantId": "vdevo153xxxx",
  "applicantName": "小明的玩具",
  "applicantAvatar": "https://.../a.png",
  "targetType": 1,                 // 被申请方类型
  "targetId": "ay16289xxxx",
  "targetName": "小红妈妈",
  "targetAvatar": "https://.../b.png",
  "applicantAdminStatus": "APPROVED",  // 申请方侧审批状态
  "targetAdminStatus": "PENDING",      // 被申请方侧审批状态
  "gmtCreate": 1783407368538
}
```

---

## 5. 错误码

| 错误码 | 标识 | 说明 |
|---|---|---|
| 13890601 | CONTACT_ROOM_NOT_EXIST | 房间不存在或已过期(含码输错) |
| 13890602 | CONTACT_ROOM_FULL | 房间已满员 |
| 13890603 | CONTACT_ROOM_BUSY | 房间码分配繁忙,请稍后重试 |
| 13890604 | CONTACT_APPLICATION_NOT_EXIST | 好友申请不存在 |
| 13890012 | PARAMS_ILLEGAL | 参数非法 / 申请已完结 |
| 13890013 | PERMISSION_DENY | 无权限(非家庭成员/非管理员/不在房内) |
| 13890016 | DEVICE_NOT_EXIST | 设备不存在 |
| 13890017 | REQUEST_FREQUENTLY | 操作过于频繁(输错码频控拉黑) |
| 13890401 | USER_NOT_EXISTS | 用户不存在 |

---

## 6. 典型流程

### 6.1 房间加好友(全流程)

```
玩具A(房主)                    云端                     玩具B / 家长B的App
   |-- room.create ------------->|  生成4位码,TTL 120s
   |<-- {code:8642, expireAt} ---|
   |     (口头告知 8642)          |
   |                             |<-- room.join {code:8642} --|
   |                             |--- {members:[A,B]} ------->|
   |-- room.members (轮询) ----->|
   |<-- {members:[A,B]} ---------|
   |-- room.apply {targets:["0:devB"]} -->|  创建申请 PENDING/PENDING
   |                             |
   |        (双方家庭管理员在 App 上)        |
   |                             |<-- application/list -------|
   |                             |<-- application/review -----|  双侧 APPROVED
   |                             |  互写双方通讯录(source=1)
   |-- contact.list ------------>|
   |<-- 通讯录含玩具B ------------|  → 可发起 RTC 呼叫
```

### 6.2 房间销毁时机

1. TTL 到期(默认 120s)自动销毁,无需任何调用;
2. 房主调用 `room.leave` 立即解散;
3. 销毁后 4 位码回收,可能被新房间复用——端上持旧码操作会收到 `13890601`,应引导重建房间。

### 6.3 NFC 加好友

`thing.ai.contact.apply` 创建申请后,后续审批与互写流程与房间途径完全一致(同一张申请表、同一个审批接口)。
