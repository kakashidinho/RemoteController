//
//  RemoteViewController.m
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "RemoteViewController.h"

typedef enum RemoteEventType {
	REMOTE_TOUCH_BEGAN,
	REMOTE_TOUCH_MOVED,
	REMOTE_TOUCH_ENDED,
	REMOTE_TOUCH_CANCELLED,
	
	REMOTE_RECORD_START,
	REMOTE_RECORD_END,
	
	REMOTE_SCREENSHOT_CAPTURE
} RemoteEventType;

typedef struct RemoteEvent {
	RemoteEventType type;
	
	union {
		struct {
			int id;
			float x;
			float y;
		} touchData;
	};
} RemoteEvent;

@interface RemoteViewController ()

@property (strong, atomic) dispatch_queue_t backGroundDispatchQueue;
@property (strong, atomic) NSMutableDictionary* touchesDictionary;
@property (atomic) int touchIdCounter;
@property (strong, atomic) NSMutableArray* reusableTouchIds;

@property (atomic) uint64_t frameCounter;
@property (atomic) uint64_t lastRenderedFrame;

@end

@implementation RemoteViewController

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
	self.frameCounter = self.lastRenderedFrame = 0;
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
	if (self.networkHandler)
	{
		[self.networkHandler close];
		self.networkHandler = nil;
	}
	else
		[self dismissViewControllerAnimated:YES completion:nil];
}

- (IBAction)flipYBtnClicked:(id)sender {
	self.flipYBtn.selected = !self.flipYBtn.selected;
}

- (IBAction)captureScreenshot:(id)sender {
	//send event to remote side
	RemoteEvent event;
	event.type = REMOTE_SCREENSHOT_CAPTURE;
	
	[self sendRemoteEvent:&event];
	
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
	
	RemoteEvent event;
	if (self.recordBtn.selected)
	{
		event.type = REMOTE_RECORD_START;
	}
	else
		event.type = REMOTE_RECORD_END;
	
	[self sendRemoteEvent:&event];
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

- (void) sendRemoteEvent: (RemoteEvent*) event {
	if (self.networkHandler) {
		NSData* data = [NSData dataWithBytes:event length:sizeof(RemoteEvent)];
		[self.networkHandler sendData:data];
	}
}

//touch events
- (void) touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:REMOTE_TOUCH_BEGAN];
	}//for (UITouch* touch in touches)
}

- (void) touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:REMOTE_TOUCH_MOVED];
	}//for (UITouch* touch in touches)
}

- (void) touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:REMOTE_TOUCH_ENDED];
	}//for (UITouch* touch in touches)
}

- (void) touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch* touch in touches) {
		[self sendTouchEventToRemoteIfNeeded: touch type:REMOTE_TOUCH_CANCELLED];
	}//for (UITouch* touch in touches)
}

- (void) sendTouchEventToRemoteIfNeeded: (UITouch*) touch
								   type: (RemoteEventType) type {
	
	CGPoint pointInFrame = [touch locationInView:self.remoteFrameView];
	
	if (![self.remoteFrameView pointInside:pointInFrame withEvent:nil])
	{
		if ([self hasTouchIdFor: touch])
			type = REMOTE_TOUCH_CANCELLED;
		else
			return;//ignore
	}
	
	CGRect remoteFrameViewRect = self.remoteFrameView.frame;
	
	//TODO: assume both sides use the same byte order for now
	RemoteEvent event;
	event.type = type;
	event.touchData.id = [self getOrCreateTouchId:touch];
	
	event.touchData.x = pointInFrame.x / remoteFrameViewRect.size.width * self.remoteFrameSize.width;
	event.touchData.y = pointInFrame.y / remoteFrameViewRect.size.height * self.remoteFrameSize.height;
	
	if (self.flipYBtn.selected)
		event.touchData.y = self.remoteFrameSize.height - event.touchData.y;
	
	[self sendRemoteEvent:&event];
	
	if (type == REMOTE_TOUCH_CANCELLED || type == REMOTE_TOUCH_ENDED) {
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

//stream delegate
- (id<NetworkHandlerDelegate>) onNetworkClosedWithError: (NSString*) errorMsg {
	[super onNetworkClosedWithError:errorMsg];
	
	[self dismissViewControllerAnimated:YES completion:nil];
	
	return nil;
}

- (id<NetworkHandlerDelegate>) onReceivedData: (NSData*) data {
	uint64_t frameId = self.frameCounter ++;
	
	dispatch_async(self.backGroundDispatchQueue, ^{
		//decode the image data in background
		UIImage *image = [[UIImage alloc] initWithData:data];
		
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
				if (self.lastRenderedFrame >= frameId)//skip
					return;
				self.remoteFrameView.image = image;
				
				self.lastRenderedFrame = frameId;
			});
		}
	});
	
	return self;
}

//destructor
- (void) dealloc {
}

@end
