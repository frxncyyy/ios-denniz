/* Regression for SpringBoard's R211 presentDrawable: exception. */
static unsigned drawableCalls,drawableDestroyed;
@interface TestDrawable : NSObject { @public TeguCommand *command; unsigned mode; double time; }
@end
@implementation TestDrawable
- (void)present { assert(command->status==3);assert(mode==0);drawableCalls++; }
- (void)presentAtTime:(double)value { assert(command->status==3);assert(mode==1&&value==time);drawableCalls++; }
- (void)presentAfterMinimumDuration:(double)value { assert(command->status==3);assert(mode==2&&value==time);drawableCalls++; }
- (void)dealloc { drawableDestroyed++;[super dealloc]; }
@end
static void test_drawable_presentation(void) {
 unsigned before=live;drawableCalls=drawableDestroyed=0;
 TeguComputeDevice *d=[TeguComputeDevice alloc];TeguQueue *q=[d newCommandQueue];
 for(unsigned test=0;test<5;test++) {
  TeguCommand *c=[[q commandBuffer] retain];TestDrawable *v=[TestDrawable alloc];
  v->command=c;v->mode=test<3?test:0;v->time=0.125;
  if(test==1)[c presentDrawable:v atTime:v->time];
  else if(test==2)[c presentDrawable:v afterMinimumDuration:v->time];
  else [c presentDrawable:v];
  unsigned calls=drawableCalls,destroyed=drawableDestroyed;
  [v release];assert(drawableDestroyed==destroyed&&drawableCalls==calls);
  if(test==3)TEGU_INVALID(c);
  if(test!=4){[c commit];[c waitUntilCompleted];[c commit];
   assert(drawableCalls==calls+(test<3));assert(drawableDestroyed==destroyed+1);}
  [c release];drain();assert(drawableDestroyed==destroyed+1);
 }
 [q release];[d release];drain();assert(live==before);
 assert(drawableCalls==3&&drawableDestroyed==5);
 puts("PASS drawable presentation: deferred selectors, timing arguments, retention, exactly once, failure and uncommitted cleanup");
}
