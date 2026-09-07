/* SPDX-License-Identifier: MIT */
/* Execute the production Linux adapter with only OS/DT/MMIO primitives mocked. */
#include "tdvp-cpu1-linux-owner-mock.h"
#include "linux/tdvp_cpu1_owner.c"
#include "tdvp_vision_owner_io.h"

enum { ROOT, MMZ, SHARED, GPIO, POWER, CMU, PLL0, PLL1, PLL2, ISPDDR, AIDDR, FIRST_DISABLED };
static struct device_node nodes[] = {
    {.path="/cpu1-vision"}, {.path="/reserved-memory/cpu1-mmz@14000000"},
    {.path="/reserved-memory/cpu1-transport@1c000000"}, {.path="/soc/gpio@9140b000"},
    {.path="/soc/sysctl/sysctl_power@91103000"}, {.path="/soc/sysctl/sysctl_clock@91100000"},
    {.path="/soc/sysctl/sysctl_boot@91102000/pll0_div4"},
    {.path="/soc/sysctl/sysctl_boot@91102000/pll1_div4"},
    {.path="/soc/sysctl/sysctl_boot@91102000/pll2_div4"},
    {.path="/soc/sysctl/sysctl_clock@91100000/tdvp_isp_ddr"},
    {.path="/soc/sysctl/sysctl_clock@91100000/tdvp_ai_ddr"},
    {.path="/soc/i2c@91409000"}, {.path="/soc/i2c@91409000/gc2093@37"}, {.path="/soc/isp.0"},
    {.path="/soc/mipi.0"}, {.path="/soc/mipi.1"}, {.path="/soc/mipi.2"},
    {.path="/soc/gnne@80400000"}, {.path="/soc/ai2d@80400c00"},
    {.path="/soc/sysctl/sysctl_clock@91100000/i2c4_clk"},
    {.path="/soc/sysctl/sysctl_clock@91100000/i2c4_pclk_gate"},
    {.path="/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1"},
    {.path="/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1_mux"},
    {.path="/soc/sysctl/sysctl_clock@91100000/ai_clk"},
    {.path="/soc/sysctl/sysctl_clock@91100000/ai_aclk"},
};
static struct device dev = {.of_node=&nodes[ROOT]}, domains[2];
static struct clk clocks[5] = {{0}, {1}, {2}, {3}, {4}};
static struct tdvp_owner_control wire;
static struct tdvp_linux_owner owner;
static struct reserved_mem reservation;
static unsigned long rates[5];
static unsigned int stage, fail_stage, attached, powered, acquired, enabled, exclusive;
static unsigned int writes, random_calls, refs, seq_reads;
static unsigned int claimed;
static struct resource claimed_resources[3];
unsigned int warnings;
static int bad_supplier, bad_count, bad_name, bad_binding, assigned, wrong_reservation;
static int bad_ddr_layout;
static u64 now;
static bool tear_snapshot;
static bool fail(void) { return ++stage == fail_stage; }

static void reset(void)
{
    unsigned int i;
    owner = (struct tdvp_linux_owner){0}; wire = (struct tdvp_owner_control){0};
    stage=fail_stage=attached=powered=acquired=enabled=exclusive=writes=random_calls=refs=seq_reads=warnings=0;
    claimed=0;
    bad_supplier=bad_count=bad_name=bad_binding=assigned=wrong_reservation=0;
    bad_ddr_layout=0;
    tear_snapshot=false; now=1000;
    for (i=0; i<ARRAY_SIZE(nodes); ++i) {
        nodes[i].available=i<FIRST_DISABLED;
        nodes[i].nomap=true; nodes[i].reusable=false; nodes[i].registered=true;
    }
    nodes[MMZ].base=TDVP_VISION_MMZ_BASE; nodes[MMZ].size=TDVP_VISION_MMZ_SIZE;
    nodes[SHARED].base=TDVP_VISION_SHARED_BASE; nodes[SHARED].size=TDVP_VISION_SHARED_SIZE;
    nodes[GPIO].base=0x9140b000; nodes[POWER].base=0x91103000; nodes[CMU].base=0x91100000;
    rates[0]=400000000; rates[1]=594000000; rates[2]=666000000;
    rates[3]=rates[4]=400000000;
}

int of_property_match_string(struct device_node *n, const char *p, const char *s)
{
    assert(n==dev.of_node);
    if (bad_name) return -EINVAL;
    if (!strcmp(p,"memory-region-names")) return !strcmp(s,"transport") ? 0 : 1;
    if (!strcmp(p,"clock-names")) return !strcmp(s,"isp-ddr") ? 3 : !strcmp(s,"ai-ddr") ? 4 : s[3]-'0';
    assert(!strcmp(p,"power-domain-names")); return !strcmp(s,"ai") ? 0 : 1;
}
struct device_node *of_parse_phandle(struct device_node *n, const char *p, int i)
{
    if (n==&nodes[ISPDDR] || n==&nodes[AIDDR]) { assert(!strcmp(p,"clocks") && !i); ++refs; return &nodes[PLL0]; }
    assert(n==dev.of_node);
    ++refs;
    if (!strcmp(p,"memory-region")) return &nodes[i ? MMZ : SHARED];
    assert(i==0);
    if (!strcmp(p,"tdvp,gpio-controller")) return &nodes[GPIO];
    if (!strcmp(p,"tdvp,power-controller")) return &nodes[POWER];
    assert(!strcmp(p,"tdvp,clock-controller")); return &nodes[CMU];
}
struct reserved_mem *of_reserved_mem_lookup(struct device_node *n)
{
    reservation=(struct reserved_mem){n->base, n->size + (wrong_reservation ? 4096 : 0)};
    return n->registered ? &reservation : NULL;
}
bool of_device_is_available(struct device_node *n) { return n->available; }
bool of_property_read_bool(struct device_node *n, const char *p)
{ return !strcmp(p,"no-map") ? n->nomap : n->reusable; }
int of_property_read_u32(struct device_node *n, const char *p, u32 *v)
{
    assert(n==&nodes[ISPDDR] || n==&nodes[AIDDR]);
    *v=!strcmp(p,"clk-gate-reg-offset") ? 0x60 : !strcmp(p,"clk-gate-reg-bit-enable") ? (n==&nodes[ISPDDR] ? 4 : 6) : 0;
    if (bad_ddr_layout) ++*v;
    return 0;
}
int of_address_to_resource(struct device_node *n, int i, struct resource *r)
{ assert(i==0); *r=(struct resource){n->base,n->base+n->size-1}; return 0; }
void of_node_put(struct device_node *n) { if(n) { assert(refs); --refs; } }
struct device_node *of_find_node_by_path(const char *p)
{
    unsigned int i;
    for(i=0;i<ARRAY_SIZE(nodes);++i) if(!strcmp(nodes[i].path,p)) { ++refs; return &nodes[i]; }
    return NULL;
}
int of_count_phandle_with_args(struct device_node *n, const char *p, const char *c)
{ (void)n; (void)c; return bad_count ? 0 : (!strcmp(p,"clocks") ? 5 : 2); }
int of_property_count_strings(struct device_node *n, const char *p)
{ (void)n; return !strcmp(p,"clock-names") ? 5 : 2; }
void *of_find_property(struct device_node *n, const char *p, int *len)
{ (void)n; (void)p; (void)len; return assigned ? &dev : NULL; }
int of_parse_phandle_with_args(struct device_node *n, const char *p, const char *c, int i,
                                struct of_phandle_args *a)
{
    (void)n; (void)c; ++refs;
    if(!strcmp(p,"clocks")) *a=(struct of_phandle_args){&nodes[PLL0+i],0,{0}};
    else *a=(struct of_phandle_args){&nodes[POWER],1,{i+1}};
    if(bad_binding) a->np=&nodes[GPIO];
    return 0;
}
bool k230_tdvp_gpio_ready(struct device_node *n) { assert(n==&nodes[GPIO]); return bad_supplier!=1; }
bool k230_tdvp_power_ready(struct device_node *n) { assert(n==&nodes[POWER]); return bad_supplier!=2; }
bool k230_tdvp_clock_ready(struct device_node *n) { assert(n==&nodes[CMU]); return bad_supplier!=3; }
void *devm_ioremap(struct device *d, phys_addr_t base, size_t bytes)
{ assert(d==&dev && base==TDVP_OWNER_BASE && bytes==TDVP_OWNER_WINDOW); return fail() ? NULL : &wire; }
struct resource *request_mem_region(resource_size_t base, resource_size_t bytes, const char *name)
{
    /* Independent literal map: changing the production range must fail here. */
    assert(claimed<3 && base==0x80000000ULL+claimed*0x200000ULL);
    assert(bytes==(claimed==2 ? 0x1000ULL : 0x200000ULL));
    assert(!strcmp(name, claimed==0 ? "tdvp-cpu1-kpu-sram" :
                        claimed==1 ? "tdvp-cpu1-shared-sram" : "tdvp-cpu1-gnne-fft-ai2d"));
    assert(!writes && !attached && !powered && !acquired);
    if (fail()) return NULL;
    claimed_resources[claimed]=(struct resource){base,base+bytes-1};
    return &claimed_resources[claimed++];
}
void release_mem_region(resource_size_t base, resource_size_t bytes)
{
    assert(claimed && !owner.started && !attached && !powered && !acquired && !enabled && !exclusive);
    --claimed;
    assert(claimed_resources[claimed].start==base && resource_size(&claimed_resources[claimed])==bytes);
}
struct device *dev_pm_domain_attach_by_name(struct device *d, const char *n)
{ assert(d==&dev); if(fail()) return ERR_PTR(-EIO); ++attached; return &domains[!strcmp(n,"disp")]; }
void dev_pm_domain_detach(struct device *d, bool on)
{ (void)d; assert(on && attached); --attached; }
int pm_runtime_resume_and_get(struct device *d)
{ (void)d; if(fail()) return -EIO; ++powered; return 0; }
void pm_runtime_put_sync(struct device *d) { (void)d; assert(powered); --powered; }
int clk_bulk_get(struct device *d, int count, struct clk_bulk_data *c)
{ int i; assert(d==&dev && count==5); if(fail()) return -EIO; for(i=0;i<count;++i)c[i].clk=&clocks[i]; acquired=1; return 0; }
void clk_bulk_put(int n, struct clk_bulk_data *c) { (void)c; assert(n==5 && acquired && !enabled && !exclusive); acquired=0; }
int clk_bulk_prepare_enable(int n, struct clk_bulk_data *c)
{ (void)c; assert(n==5 && acquired && exclusive==5); if(fail())return -EIO; enabled=1; return 0; }
void clk_bulk_disable_unprepare(int n, struct clk_bulk_data *c)
{ (void)c; assert(n==5 && enabled); enabled=0; }
int clk_rate_exclusive_get(struct clk *c) { (void)c; if(fail())return -EIO; ++exclusive; return 0; }
void clk_rate_exclusive_put(struct clk *c) { (void)c; assert(exclusive); --exclusive; }
unsigned long clk_get_rate(struct clk *c) { return rates[c->id]; }
u64 ktime_get(void) { return now; }
u64 get_random_u64(void) { return ++random_calls==1 ? 0 : 0x123456789abcdef0ULL; }
u32 readl(const u32 *p)
{
    u32 value=*p;
    if(tear_snapshot && p==&wire.cpu1_side.sequence && ++seq_reads==1)
        wire.cpu1_side.sequence+=2;
    return value;
}
u64 readq(const u64 *p) { return *p; }
void writel(u32 v, u32 *p) { ++writes; *p=v; }
void writeq(u64 v, u64 *p) { ++writes; *p=v; }

static void clean_failure(void)
{
    assert(tdvp_linux_owner_prepare(&dev,&owner)<0);
    tdvp_linux_owner_abort(&owner); /* caller cleanup remains idempotent */
    assert(!writes && !refs && !attached && !powered && !acquired && !enabled && !exclusive && !warnings);
    assert(!claimed && !owner.ai_regions);
}

int main(void)
{
    unsigned int i, steps, cases=0, old_writes;
    struct tdvp_owner_session cpu;
    struct tdvp_owner_record snapshot;
    reset(); assert(!tdvp_linux_owner_prepare(&dev,&owner)); steps=stage;
    assert(!writes && !refs && attached==2 && powered==2 && exclusive==5 && enabled);
    assert(claimed==3 && owner.ai_regions==3);
    tdvp_linux_owner_abort(&owner); assert(!attached && !powered && !exclusive && !enabled && !acquired);
    assert(!claimed && !owner.ai_regions);
    for(i=1;i<=steps;++i) { reset(); fail_stage=i; clean_failure(); ++cases; }
    for(i=FIRST_DISABLED;i<ARRAY_SIZE(nodes);++i) { reset(); nodes[i].available=true; clean_failure(); ++cases; }
    for(i=MMZ;i<=SHARED;++i) {
        reset(); nodes[i].nomap=false; clean_failure(); ++cases;
        reset(); nodes[i].reusable=true; clean_failure(); ++cases;
        reset(); nodes[i].registered=false; clean_failure(); ++cases;
        reset(); nodes[i].base+=4096; clean_failure(); ++cases;
        reset(); nodes[i].size-=4096; clean_failure(); ++cases;
    }
    for(i=1;i<=3;++i) { reset(); bad_supplier=i; clean_failure(); ++cases; }
    for(i=0;i<5;++i) { reset(); --rates[i]; clean_failure(); ++cases; }
    reset(); bad_ddr_layout=1; clean_failure(); ++cases;
    reset(); rates[2]=666750001; clean_failure(); ++cases;
    reset(); bad_count=1; clean_failure(); ++cases;
    reset(); bad_name=1; clean_failure(); ++cases;
    reset(); bad_binding=1; clean_failure(); ++cases;
    reset(); assigned=1; clean_failure(); ++cases;
    reset(); wrong_reservation=1; clean_failure(); ++cases;
    reset(); assert(!tdvp_linux_owner_prepare(&dev,&owner));
    owner.ai_regions=2; tdvp_linux_owner_start(&owner);
    assert(warnings==1 && !writes && !random_calls && !owner.started); ++cases;
    owner.ai_regions=3; tdvp_linux_owner_abort(&owner); assert(!claimed);
    reset(); assert(!tdvp_linux_owner_prepare(&dev,&owner));
    assert(tdvp_linux_owner_status(&owner)==-EAGAIN);
    tdvp_linux_owner_start(&owner); assert(random_calls==2 && writes && wire.linux_side.state==TDVP_OWNER_OFFER);
    assert(tdvp_owner_snapshot(&wire.linux_side,&snapshot));
    assert(snapshot.cookie==0x123456789abcdef0ULL);
    tdvp_owner_cpu1_init(&cpu,0xfedcba9876543210ULL,now);
    for(i=0;i<8;++i) {
        now+=50;
        int result=tdvp_owner_cpu1_step(&cpu,&wire.linux_side,now);
        if(result==1) assert(!tdvp_owner_cpu1_ready(&cpu));
        assert(result>=0);
        if(cpu.publish) tdvp_owner_publish(&wire.cpu1_side,&cpu.own);
        assert(tdvp_linux_owner_poll(&owner)!=-EIO);
    }
    assert(!tdvp_linux_owner_status(&owner));
    assert(owner.session.peer_ready && wire.linux_side.peer_cookie==cpu.own.cookie);
    /* Linux MMIO snapshot rejects an in-flight writer and a changed sequence. */
    wire.cpu1_side.sequence|=1; assert(!tdvp_linux_owner_snapshot(&wire.cpu1_side,&snapshot));
    wire.cpu1_side.sequence++; tear_snapshot=true;
    assert(!tdvp_linux_owner_snapshot(&wire.cpu1_side,&snapshot)); tear_snapshot=false;
    assert(tdvp_linux_owner_snapshot(&wire.cpu1_side,&snapshot));
    wire.linux_side.sequence=0xfffffffeU;
    tdvp_linux_owner_publish(&wire.linux_side,&owner.session.own);
    assert(wire.linux_side.sequence==0 && tdvp_owner_snapshot(&wire.linux_side,&snapshot));
    now+=TDVP_OWNER_PEER_MS;
    assert(tdvp_linux_owner_poll(&owner)==-ETIMEDOUT);
    assert(wire.linux_side.state==TDVP_OWNER_FAULT);
    old_writes=writes; now+=50; tdvp_linux_owner_poll(&owner); assert(writes>old_writes);
    tdvp_linux_owner_abort(&owner); /* forbidden cleanup must retain live DMA resources */
    assert(warnings==1 && attached==2 && powered==2 && acquired && enabled && exclusive==5);
    assert(claimed==3 && owner.ai_regions==3);
    reset(); assert(!tdvp_linux_owner_prepare(&dev,&owner)); tdvp_linux_owner_start(&owner);
    now+=TDVP_OWNER_BOOT_MS; assert(tdvp_linux_owner_poll(&owner)==-ETIMEDOUT);
    assert(attached==2 && powered==2 && enabled && exclusive==5);
    assert(claimed==3 && owner.ai_regions==3);
    printf("Linux CPU1 ownership adapter: PASS %u preparation refusals, actual MMIO handshake, stale/odd snapshots, wrap and fault-time resource retention\n",cases);
    return 0;
}
