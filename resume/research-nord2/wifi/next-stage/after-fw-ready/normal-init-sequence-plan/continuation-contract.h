#ifndef PRIVATE_NORMAL_SEQUENCE_PLAN_CONTRACT_H
#define PRIVATE_NORMAL_SEQUENCE_PLAN_CONTRACT_H
#include <stddef.h>
#include <stdint.h>
#include "../normal-init/normal_init.h"
#include "../regulatory-policy/regulatory.h"
#include "../regulatory-evaluator/evaluator.h"
/* DESIGN ONLY: no implementation, callbacks, native authorization or RF lease.
 * Plain copied structures are cloneable. Only a separately reviewed exact
 * same-worker native session may use them with backend operations. */
#define NCN_PLAN_COMMAND_LIMIT 64u /* proposed host bound; excess refuses whole plan */
#define NCN_COMMAND_BUFFER_LIMIT 4096u /* existing owned normal command slot */
enum ncn_command_kind {
 NCN_LEGACY_PASSIVE_INFO,NCN_DOMAIN,NCN_POWER_V0,NCN_POWER_V1,
 NCN_BOARD_FRAGMENT
};
enum ncn_pure_refusal {
 NCN_NEED_ACTUAL_BASIC=1,NCN_BOARD_AUTH_OR_LIFETIME,NCN_NO_VALID_ADDRESS,
 NCN_CAP_METADATA,NCN_UNSUPPORTED_REGULATORY_BRANCH,NCN_UNRESOLVED_POWER,
 NCN_BOARD_FRAGMENT_SHAPE,NCN_WHOLE_BATCH_CREDIT,NCN_PLAN_EXTENT
};
struct ncn_command_shape {
 enum ncn_command_kind kind;
 uint8_t cid,sequence;uint16_t wire_bytes;
 unsigned item_index,item_count;
 unsigned pse_cost,ple_cost; /* selected AXI command cost1/0, never guessed ceil */
};
enum ncn_regulatory_branch {NCN_LEGACY=1,NCN_SINGLE_SKU=2};
struct ncn_policy_identity {
 enum ncn_regulatory_branch branch;
 uint16_t legacy_country; /* actual authenticated board/source decision, not tag3 default0 */
 unsigned legacy_domain_index,legacy_power_table_index;
 uint32_t v2_country; /* only actual tag3/reset branch; not legacy override */
};
struct ncn_pure_plan {
 struct nni_address address; /* PRIVATE actual bytes, never telemetry */
 struct nrp_caps capabilities;struct nrp_board board;
 struct ncn_policy_identity selected_policy;
 struct nre_result regulatory; /* V2 only; current sku0 requires legacy plan */
 struct nni_nvram fragments;
 unsigned commands;struct ncn_command_shape command[NCN_PLAN_COMMAND_LIMIT];
 uint32_t actual_pse_before,actual_pse_after;
 unsigned local_address_applied; /* host operation only, never firmwareMAC receipt */
 /* No power_ready/RF_ready/owner boolean: exact resolved power inputs and
  * immutable source identities belong to native-owned provenance, not callers. */
};
struct ncn_native_session; /* intentionally opaque, not constructible here */
/* Suggested pure preparation boundary, intentionally NOT IMPLEMENTED:
 * - receives copied actual caps + authenticated immutable full-board view;
 * - evaluates selected policy, resolves all power commands, validates every
 *   frame using one bounded scratch buffer before producing an atomic plan;
 * - returns a refusal before any backend call if any prerequisite is absent.
 * Native retains BASIC/CAP/resource/pool/board provenance outside this type.
 * Local addresses/regulatory observations never authorize scan or an interface.
 */
#endif
