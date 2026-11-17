int nwfn_finish_command(uint8_t cid)
{
 struct nwfn_native *n=&nwfn_native;struct nwt_state *t=&n->init.transport;
 size_t used=0;
 if(n->stage!=NWFN_RETAINED||nwfn_owner(n)||
    t->finish_phase>1||cid!=(t->finish_phase?0x41:0x40)||
    nwt_finish(n->finish_tx,sizeof(n->finish_tx),cid,t->next_sequence,&used))return nwfn_stop(n,19);
 if(nwt_capture(&nwfn_io,n,t,n->finish_tx,used,1,n->finish_rx,
    sizeof(n->finish_rx),&n->finish_received)||n->finish_received<34||
    n->finish_rx[32]!=0)return nwfn_stop(n,20);
 t->finish_phase++;return 0;
}
