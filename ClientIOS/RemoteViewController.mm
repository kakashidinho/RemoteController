//
//  RemoteViewController.m
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "RemoteViewController.h"

#define MAX_FRAMES_TO_PROCESS 30

#define DESIRED_FRAME_INTERVAL (1 / 30.0)

@interface RemoteViewController ()

@property (strong, atomic) dispatch_queue_t backGroundDispatchQueue;
@property (strong, atomic) NSMutableDictionary* touchesDictionary;
@property (atomic) int touchIdCounter;
@property (strong, atomic) NSMutableArray* reusableTouchIds;

@property (atomic) uint64_t frameInProcessing;

@property (atomic) uint64_t lastReceivedFrame;
@property (atomic) uint64_t lastRenderedFrame;


@property (atomic) HQRemote::time_checkpoint_t lastReceivedFrameTime;
@property (atomic) HQRemote::time_checkpoint_t lastRenderedFrameTime;

@property (atomic) double remoteFrameInterval;

@property (atomic, strong) NSTimer* connLoopTimer;

@end

@implementation RemoteViewController

@synthesize connHandler = _connHandler;
@synthesize remoteFrameSize = _remoteFrameSize;

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.
	self.backGroundDispatchQueue =  dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
									//dispatch_queue_create("RemoteViewController.BackgroundThread", NULL);
	
	[self.flipYBtn  setTitle:@"Flip Y" forState:UIControlStateSelected];
	[self.flipYBtn  setTitle:@"Flip Y" forState:UIControlStateNormal];
	self.flipYBtn.selected = NO;
	
	self.recordBtn.selected = NO;
	
	[self setRemoteFrameSize:_remoteFrameSize];//re-arrange the layout
	
	//enable multi-touch
	[self.view setMultipleTouchEnabled:YES];
	self.touchesDictionary = [NSMutableDictionary dictionary];
	self.reusableTouchIds = [NSMutableArray array];
	self.touchIdCounter = 0;//TODO: is it possible for this value to be overflowed
	
	
	//
	self.lastRenderedFrame = self.lastReceivedFrame = 0;
	self.frameInProcessing = 0;
	
	self.lastRenderedFrameTime = self.lastReceivedFrameTime = 0;
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/
- (IBAction)exitAction:(id)sender {
	[self exit];
}

- (void) exit {
	self.connHandler = nullptr;
	[self dismissViewControllerAnimated:YES completion:nil];
}

- (IBAction)flipYBtnClicked:(id)sender {
	self.flipYBtn.selected = !self.flipYBtn.selected;
}

- (IBAction)captureScreenshot:(id)sender {
	//send event to remote side
	auto eventRef = std::make_shared<HQRemote::PlainEvent>(HQRemote::SCREENSHOT_CAPTURE);
	
	[self sendRemoteEvent:eventRef];
	
	//perform screen flashing animation
	[UIView animateWithDuration:0.15
					 animations:^{self.remoteFrameView.alpha = 0.0; }
					 completion:^(BOOL finished) {
						 [UIView animateWithDuration:0.15
										  animations:^{self.remoteFrameView.alpha = 1.0; }];
					 }];
}

- (IBAction)recordBtnClicked:(id)sender {
	self.recordBtn.selected = !self.recordBtn.selected;
	
	auto eventRef = std::make_shared<HQRemote::PlainEvent>();
	if (self.recordBtn.selected)
	{
		eventRef->event.type = HQRemote::RECORD_START;
	}
	else
		eventRef->event.type = HQRemote::RECORD_END;
	
	[self sendRemoteEvent:eventRef];
}

- (CGSize) remoteFrameSize {
	return _remoteFrameSize;
}

- (void) setRemoteFrameSize:(CGSize)remoteFrameSize {
	_remoteFrameSize = remoteFrameSize;
	
	//re-arrange the layout
	CGRect containerRect = self.view.frame;
	CGRect remoteFrameViewRect  = self.remoteFrameView.frame;
	CGRect exitBtnRect = self.exitBtn.frame;

	CGFloat maxWidth = containerRect.size.width * 1.0f;
	CGFloat maxHeight = (exitBtnRect.origin.y - remoteFrameViewRect.origin.y - 50.f);
	
	CGFloat widthRatio = maxWidth / _remoteFrameSize.width;
	CGFloat heightRatio = maxHeight / _remoteFrameSize.height;
	CGFloat ratio = MIN(widthRatio, heightRatio);
	
	remoteFrameViewRect.size.width = ratio * _remoteFrameSize.width;
	remoteFrameViewRect.size.height = ratio * _remoteFrameSize.height;
	
	remoteFrameViewRect.origin.x = containerRect.size.width * 0.5f - remoteFrameViewRect.size.width * 0.5f;
	
	self.remoteFrameView.translatesAutoresizingMaskIntoConstraints = YES;
	self.remoteFrameView.frame = remoteFrameViewRect;
	
	//TODO: resize when the screen is rotated
}

-(void)viewDidLayoutSubviews
{
}

- (std::shared_ptr<HQRemote::IConnectionHandler>) connHandler {
	return _connHandler;
}

- (void) setConnHandler: (std::shared_ptr<HQRemote::IConnectionHandler>) handler {
	
	if (_connLoopTimer != nil)
	{
		[_connLoopTimer invalidate];
		
		_connLoopTimer = nil;
	}
	
	if (_connHandler)
	{
		auto stopFrameEvent = HQRemote::PlainEvent(HQRemote::STOP_SEND_FRAME);
		_connHandler->sendData(stopFrameEvent);
		_connHandler->stop();
	}
	_connHandler = handler;
	
	if (_connHandler != nullptr)
	{
		_connLoopTimer = [NSTimer scheduledTimerWithTimeInterval:0
														  target:self
														selector:@selector(connLoop)
														userInfo:nil
														 repeats:YES];
		
		auto sendFrameEvent = HQRemote::PlainEvent(HQRemote::START_SEND_FRAME);
		_connHandler->sendData(sendFrameEvent);
	}
	
}

- (void) connLoop {
	if (_connHandler->connected() == false)//disconnected
	{
		//TODO: display error
		[self exit];
	}
	else {
		auto dataRef = _connHandler->receiveData();
		if (dataRef) {
			dispatch_async(self.backGroundDispatchQueue, ^{
				auto l_dataRef = dataRef;
				auto eventRef = HQRemote::deserializeEvent(std::move(l_dataRef));
				[self handleRemoteEvent:eventRef];
			});
		}//if (dataRef)
	}
}

- (void) handleRemoteEvent: (HQRemote::EventRef) eventRef {
	if (eventRef) {
		switch (eventRef->event.type) {
			case HQRemote::RENDERED_FRAME:
			{
				HQRemote::time_checkpoint_t time;
				uint64_t frameId = eventRef->event.renderedFrameData.frameId;
				
				if (self.frameInProcessing > MAX_FRAMES_TO_PROCESS)
				{
#ifdef DEBUG
					fprintf(stderr, "discarded a frame due to too many in decompressing queue\n");
#endif
					break;//skip
				}
				self.frameInProcessing ++;
				
				//synchronize the decompression of the frame
				HQRemote::getTimeCheckPoint(time);
				double dispatchDelay = 0;
				double intendedDispatchElapsedTime = 0;
				
				if (_lastReceivedFrame != 0)
				{
					intendedDispatchElapsedTime = (frameId - _lastReceivedFrame) * _remoteFrameInterval;
				}
				
				if (_lastReceivedFrameTime != 0)
				{
					auto elapsed = HQRemote::getElapsedTime(_lastReceivedFrameTime, time);
					if (elapsed < intendedDispatchElapsedTime)
						dispatchDelay = intendedDispatchElapsedTime - elapsed;
				}//if (_lastRenderedFrameTime != 0)
				
				_lastReceivedFrameTime = time;
				_lastReceivedFrame = frameId;
				
				auto decompressBlock = ^{
					auto &event = eventRef->event;
					//decode the image data in background
					NSData* nsFrameData = [NSData dataWithBytesNoCopy:event.renderedFrameData.frameData
															   length:event.renderedFrameData.frameSize
														 freeWhenDone:NO];
					UIImage *image = [[UIImage alloc] initWithData:nsFrameData];
					
					// Decompress image
					if (image) {
						CGSize size = self.remoteFrameView.frame.size;
						
						UIGraphicsBeginImageContext(size);
						
						//[image drawInRect:CGRectMake(0, 0, size.width, size.height)];
						//this will draw  the image flipped
						CGContextDrawImage(UIGraphicsGetCurrentContext(), CGRectMake(0, 0, size.width, size.height), image.CGImage);
						
						image = UIGraphicsGetImageFromCurrentImageContext();
						
						UIGraphicsEndImageContext();
						
						//update the view on ui thread
						dispatch_async(dispatch_get_main_queue(), ^{
							self.frameInProcessing--;
							
							if (_lastRenderedFrame >= frameId)//skip
							{
#ifdef DEBUG
								fprintf(stderr, "discarded a frame\n");
#endif
								return;
							}
							
							//synchronize the display of the frame
							HQRemote::time_checkpoint_t time;
							HQRemote::getTimeCheckPoint(time);
							
							double delay = 0;
							double intendedElapsedTime = 0;
							
							if (_lastRenderedFrame != 0)
							{
								intendedElapsedTime = (frameId - _lastRenderedFrame) * _remoteFrameInterval;
							}
							
							if (_lastRenderedFrameTime != 0)
							{
								auto elapsed = HQRemote::getElapsedTime(_lastRenderedFrameTime, time);
								if (elapsed < intendedElapsedTime)
									delay = intendedElapsedTime - elapsed;
							}//if (_lastRenderedFrameTime != 0)
							
							if (delay > 0)
							{
								dispatch_after(delay, dispatch_get_main_queue(), ^{
									if (_lastRenderedFrame >= frameId)//skip
									{
#ifdef DEBUG
										fprintf(stderr, "discarded a frame (async)\n");
#endif
										return;
									}
									
									_remoteFrameView.image = image;
									
									_lastRenderedFrame = frameId;
									HQRemote::getTimeCheckPoint(_lastRenderedFrameTime);
								});
							}
							else
							{
								_remoteFrameView.image = image;
								
								_lastRenderedFrame = frameId;
								_lastRenderedFrameTime = time;
							}
						});
					}
				};//decompressBlock
				
				if (dispatchDelay > 0)
				{
					dispatch_after(dispatchDelay, self.backGroundDispatchQueue, decompressBlock);
				}
				else {
					dispatch_async(self.backGroundDispatchQueue, decompressBlock);
				}
			}
				break;//case HQRemote::RENDERED_FRAME
			case HQRemote::COMPRESSED_EVENTS:
			{
				auto compressedEventRef = std::static_pointer_cast<HQRemote::CompressedEvents>(eventRef);
				
				for (auto & event: *compressedEventRef) {
					[self handleRemoteEvent:event];
				}
			}
				break;
			case HQRemote::FRAME_INTERVAL:
				_remoteFrameInterval = eventRef->event.frameInterval;
				break;
			default:
				break;
		}
	}//if (eventRef)
}

- (void) sendRemoteEvent: (HQRemote::EventRef) eventRef {
	if (_connHandler) {
		_connHandler->sendData(*eventRef);
	}
}

//touch events
- (void) touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:HQRemote::TOUCH_BEGAN];
	}//for (UITouch* touch in touches)
}

- (void) touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:HQRemote::TOUCH_MOVED];
	}//for (UITouch* touch in touches)
}

- (void) touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:HQRemote::TOUCH_ENDED];
	}//for (UITouch* touch in touches)
}

- (void) touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:HQRemote::TOUCH_CANCELLED];
	}//for (UITouch* touch in touches)
}

- (void) sendTouchEventToRemoteIfNeeded: (UITouch*) touch
								   type: (HQRemote::EventType) type {
	
	CGPoint pointInFrame = [touch locationInView:self.remoteFrameView];
	
	if (![self.remoteFrameView pointInside:pointInFrame withEvent:nil])
	{
		if ([self hasTouchIdFor: touch])
			type = HQRemote::TOUCH_CANCELLED;
		else
			return;//ignore
	}
	
	CGRect remoteFrameViewRect = self.remoteFrameView.frame;
	
	//TODO: assume both sides use the same byte order for now
	auto eventRef = std::make_shared<HQRemote::PlainEvent>(type);
	auto &event = eventRef->event;
	
	event.touchData.id = [self getOrCreateTouchId:touch];
	
	event.touchData.x = pointInFrame.x / remoteFrameViewRect.size.width * self.remoteFrameSize.width;
	event.touchData.y = pointInFrame.y / remoteFrameViewRect.size.height * self.remoteFrameSize.height;
	
	if (self.flipYBtn.selected)
		event.touchData.y = self.remoteFrameSize.height - event.touchData.y;
	
	[self sendRemoteEvent:eventRef];
	
	if (type == HQRemote::TOUCH_CANCELLED || type == HQRemote::TOUCH_ENDED) {
		NSValue* touchKey = [NSValue valueWithPointer:(__bridge const void * _Nullable)(touch)];
		
		//remove from id table
		[self.touchesDictionary removeObjectForKey:touchKey];
		
		//insert to reusable list
		NSNumber* idValue = [NSNumber numberWithInt:event.touchData.id];
		[self.reusableTouchIds addObject:idValue];
	}
}

- (BOOL) hasTouchIdFor: (UITouch*) touch {
	NSValue* touchKey = [NSValue valueWithPointer:(__bridge const void * _Nullable)(touch)];
	NSNumber* existId = [self.touchesDictionary objectForKey:touchKey];
	
	return existId != nil;
}

- (int) getOrCreateTouchId: (UITouch*) touch {
	NSValue* touchKey = [NSValue valueWithPointer:(__bridge const void * _Nullable)(touch)];
	NSNumber* existId = [self.touchesDictionary objectForKey:touchKey];
	if (existId != nil)
		return existId.intValue;
	else {
		//create new id
		int newId;
		//check if we can use any reusable id
		if (self.reusableTouchIds.count > 0)
		{
			NSNumber* reusableId = [self.reusableTouchIds objectAtIndex:0];
			newId = reusableId.intValue;
			[self.reusableTouchIds removeObjectAtIndex:0];
		}
		else {
			newId = self.touchIdCounter++;
		}
		
		//insert to id table
		NSNumber* idValue = [NSNumber numberWithInt:newId];
		[self.touchesDictionary setObject:idValue forKey:touchKey];
		
		return newId;
	}
}

//destructor
- (void) dealloc {
}

@end
