/*the functional correctness test*/
#include <autoconf.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sel4/sel4.h>
#include <sel4utils/vspace.h>
#include <sel4utils/process.h>
#include <sel4utils/mapping.h>
#include <vka/object.h>
#include <vka/capops.h>
#include <simple/simple.h>

#include "manager.h"

bench_env_t sender; 
bench_env_t receiver; 
/*ep for syn and reply*/
static vka_object_t syn_ep, r_ep, s_ep; 

#if 0
/*copy kernel image related caps*/
static int copy_kcaps(bench_ki_t *kimage) {
    //seL4_CPtr kernel_image_copy, kernel_mem_copy; 
    int ret; 
    seL4_CPtr ik_image; 
    seL4_MessageInfo_t tag, output_tag;
    seL4_CPtr *kernel_mems; 

    vka_cspace_make_path(&env->vka, ki.cptr, &src_path);
    ret = vka_cspace_alloc(&env->vka, &kernel_image_copy); 
    assert(ret == 0); 
    vka_cspace_make_path(&env->vka, kernel_image_copy, &dest_path); 
    
    /*cannot copy a kernel image cap that is not created*/
    ret = vka_cnode_copy(&dest_path, &src_path, seL4_AllRights); 


    vka_cspace_make_path(&env->vka, kernel_mem_obj.cptr, &src_path);
    ret = vka_cspace_alloc(&env->vka, &kernel_mem_copy); 
    assert(ret == 0); 
    vka_cspace_make_path(&env->vka, kernel_mem_copy, &dest_path);
    /*can copy a kernel memory cap without generated a kernel image
      with the memory*/
    ret = vka_cnode_copy(&dest_path, &src_path, seL4_AllRights); 

    assert(ret == 0);



}

#endif


#ifdef CONFIG_MULTI_KERNEL_IMAGES 
 

static int destroy_ki(m_env_t *env, bench_ki_t *kimage) {
    cspacepath_t src;
    int ret; 
    vka_cspace_make_path(&env->vka, kimage->ki.cptr, &src);  
  
    /*revoke first*/ 
    ret = vka_cnode_revoke(&src); 
    if (ret) 
        return ret; 

    return  vka_cnode_delete(&src); 
 
}



static int destroy_ki_via_kmem(m_env_t *env, bench_ki_t *kimage) {

    /*delete the kmem first
     then delete the kimage*/
    cspacepath_t src;
    int ret; 

    for (int i = 0; i < kimage->k_size; i++)  {
        vka_cspace_make_path(&env->vka, kimage->kmems[i].cptr, &src);  

        ret = vka_cnode_delete(&src); 
        if (ret) 
            return ret; 
    }

    return  destroy_ki(env, kimage); 
}


static int test_destroy(m_env_t *env) {
    /*deleting the kernel that used as the current kernel in other cores, 
     expecting other core running the idle threads*/
    int ret; 
    int __attribute__((unused)) i = 0;
    
    ret = destroy_ki_via_kmem(env, env->kimages); 
    if (ret) 
        return ret; 

    ret = destroy_ki_via_kmem(env, env->kimages + 1); 
    if (ret) 
        return ret; 

    ret = destroy_ki(env, env->kimages); 
    if (ret) 
        return ret; 

   
    ret = destroy_ki(env, env->kimages + 1);
    if (ret) 
        return ret; 
#if (CONFIG_MAX_NUM_NODES > 1)

    /*wait for 1ms letting the kernel does the schedule*/ 
    sw_sleep(1); 

    /*check the threads are alive, sperate core*/

    /*also including two threads on different core, cross-core IPC*/
    
    while(1) {
        if (alive(env) == BENCH_SUCCESS) 
            printf("alive %d\n", i); 
        else 
            printf("dead %d\n", i); 
        i++;
    }

#else 
    /*two threads same core, IPC*/
    wait_msg(r_ep.cptr); 
    printf("receiver finished\n");
    /*sender*/
    wait_msg(s_ep.cptr); 
    printf("sender finished\n");

#endif

    /*deleting the kernel image, single core, not the current kernel*/
    /*the threads are no longer runnable, the kernel image become invalid*/


    /*delete the kernel memory, expecting the same behaviour as deleting 
     kernel image*/

    return BENCH_SUCCESS;

}


#endif



/*entry point*/
void launch_bench_func_test(m_env_t *env){
    int ret; 
    uint32_t share_phy; 

    env->ipc_vka = &env->vka;

    sender.image = receiver.image = CONFIG_BENCH_THREAD_NAME;
    sender.vspace = receiver.vspace = &env->vspace;
    sender.name = "sender"; 
    receiver.name = "receiver";
#ifdef CONFIG_LIB_SEL4_CACHECOLOURING
    sender.vka = env->vka_colour; 
    receiver.vka = env->vka_colour + 1;
#else
    sender.vka = receiver.vka = &env->vka; 
#endif 
    /*ep for communicate*/
    ret = vka_alloc_endpoint(env->ipc_vka, &syn_ep);
    assert(ret == 0);
    /*ep for receiver to manager*/
    ret = vka_alloc_endpoint(env->ipc_vka, &r_ep); 
    assert(ret == 0);
    /*ep for sender to manager*/
    ret = vka_alloc_endpoint(env->ipc_vka, &s_ep);
    assert(ret == 0);

    sender.ipc_vka = receiver.ipc_vka = env->ipc_vka; 
    receiver.ep  = sender.ep = syn_ep; 
    receiver.reply_ep = r_ep;
    sender.reply_ep = s_ep; 
 
    receiver.prio  = 100;
    sender.prio = 100;
    
    /*set the actual testing num in bench_common.h*/
    receiver.test_num = BENCH_FUNC_RECEIVER;
    sender.test_num = BENCH_FUNC_SENDER; 

#ifdef CONFIG_MULTI_KERNEL_IMAGES 
    /*assign two kernel image to two threads*/
    sender.kernel = env->kimages[0].ki.cptr;
    receiver.kernel = env->kimages[1].ki.cptr; 
#else 
    /*the master kernel image*/
    sender.kernel = receiver.kernel = env->kernel;
#endif

#if (CONFIG_MAX_NUM_NODES > 1)
 
    /*two threads on two seperate cores cross*/

    sender.affinity = 1;
    receiver.affinity = 2; 

#else 

    /*two threads on same core*/ 
    sender.affinity = receiver.affinity = 0;
#endif 
    
    printf("creating sender\n"); 
    create_thread(&sender); 
    
    printf("creating receiver\n"); 
    create_thread(&receiver);

    /*recording buffer in the receiver*/
    map_r_buf(env, BENCH_FUNC_TEST_PAGES, &receiver);
    /*buffer shared between receiver and sender, owned by receiver*/
    map_shared_buf(&receiver, &sender, BENCH_FUNC_TEST_PAGES, &share_phy);

    send_run_env(&receiver, r_ep.cptr);

    printf("env send to receiver \n"); 
    send_run_env(&sender, s_ep.cptr);
    printf("env send to sender\n");
   
#if (CONFIG_MAX_NUM_NODES > 1)
 
    /*wait for 1ms letting the kernel does the schedule*/ 
    sw_sleep(1); 
    
    /*check the threads are alive, sperate core*/
    
    /*also including two threads on different core, cross-core IPC*/
    for (int i = 0; i < 1000; i++) {

        if (alive(env) == BENCH_SUCCESS) 
            printf("alive %d\n", i); 
        else 
            printf("dead %d\n", i); 
    }
                
#else 
    /*two threads same core, IPC*/
    wait_msg(r_ep.cptr); 
    printf("receiver finished\n");
    /*sender*/
    wait_msg(s_ep.cptr); 
    printf("sender finished\n");

#endif

#ifdef CONFIG_MULTI_KERNEL_IMAGES 
    test_destroy(env); 
#endif
   /*launch other kernel to the thread, to other core, the other threads are running*/

    while (1); 
}


