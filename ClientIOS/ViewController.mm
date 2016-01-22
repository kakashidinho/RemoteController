//
//  ViewController.m
//  Client
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "ViewController.h"
#import "RemoteViewController.h"

#include "../Event.h"
#include "../ConnectionHandler.h"

#include <assert.h>

#define LISTENING_PORT 123450
#define CONNECTION_TIME_OUT 10

static NSString* gIPPreferenceKey = @"IP";
static NSString* gPortPreferenceKey = @"port";

typedef enum State {
	DISCONNECTED_STATE,
	INITIALIZING_STATE,
} State;

/*-------- ViewController -----*/
@interface ViewController ()

@property (atomic) State state;
@property std::shared_ptr<HQRemote::IConnectionHandler> connHandler;
@property (atomic, strong) NSTimer* connLoopTimer;

@end

@implementation ViewController

- (void)viewDidLoad {
	[super viewDidLoad];
	// Do any additional setup after loading the view, typically from a nib.
	
	//check if we have any saved ip and port
	NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
	if (preferences == nil)
	{
		return;
	}
	
	NSString* saved_ip = [preferences objectForKey:gIPPreferenceKey];
	NSString* saved_port = [preferences objectForKey:gPortPreferenceKey];
	
	if (saved_ip != nil)
		self.ipText.text = saved_ip;
	if (saved_port != nil)
		self.portText.text = saved_port;
	
	self.progressBar.hidden = YES;
	self.state = DISCONNECTED_STATE;
}

- (void)didReceiveMemoryWarning {
	[super didReceiveMemoryWarning];
	// Dispose of any resources that can be recreated.
}

- (IBAction)connect:(id)sender {
	NSString* ip = self.ipText.text;
	NSString* portStr = self.portText.text;
	
	int port = [portStr intValue];
	
	//save to preferences
	NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
	if (preferences != nil)
	{
		[preferences setObject:ip forKey:gIPPreferenceKey];
		[preferences setObject:portStr forKey:gPortPreferenceKey];
		
		[preferences synchronize];
	}
	
	//open connection
	std::string stdip = [ip UTF8String];
	
	self.connHandler = std::make_shared<HQRemote::SocketClientHandler>(LISTENING_PORT, //tcp port
																	   LISTENING_PORT+1, //udp port
																	   HQRemote::ConnectionEndpoint(stdip, port), //remote tcp endpoint
																	   HQRemote::ConnectionEndpoint(stdip, port + 1)); //remote udp endpoint
	
	self.connHandler->start();
	
	self.connLoopTimer = [NSTimer scheduledTimerWithTimeInterval:0
													  target:self
													selector:@selector(connLoop)
													userInfo:nil
													 repeats:YES];
	[self setInProgress: YES];
	self.state = DISCONNECTED_STATE;
}

- (void) setInProgress: (BOOL) inProgress {
	if (inProgress){
		self.connectBtn.enabled = NO;
		self.progressBar.hidden = NO;
		[self.progressBar startAnimating];
	}
	else {
		self.connectBtn.enabled = YES;
		self.progressBar.hidden = YES;
		[self.progressBar stopAnimating];
	}
}

- (void) displayError: (NSString*) error {
	//display popup message
	UIAlertController *alert =
	[UIAlertController alertControllerWithTitle:@"Error"
										message:error
								 preferredStyle:UIAlertControllerStyleAlert];
	
	
	
	UIAlertAction *action = [UIAlertAction actionWithTitle:@"OK"
													 style:UIAlertActionStyleDefault
												   handler:^(UIAlertAction *action) {
												   }];
	[alert addAction:action];
	
	[self presentViewController:alert animated:YES completion:nil];
	
	[self setInProgress:NO];
}

//run loop
- (void) connLoop {
	if (_state != DISCONNECTED_STATE && _connHandler->connected() == false)//disconnected
	{
		_connHandler->stop();
		[_connLoopTimer invalidate];
		
		[self displayError:@"There was a connection error"];
		
		return;
	}
	
	switch (self.state) {
		case DISCONNECTED_STATE:
			if (_connHandler->connected()) {
				self.state = INITIALIZING_STATE;
				
				//send host info request
				HQRemote::PlainEvent plain(HQRemote::HOST_INFO);
				_connHandler->sendData(plain);
			}
			else if (_connHandler->timeSinceStart() >= CONNECTION_TIME_OUT) {
				//stop connection
				_connHandler->stop();
				[_connLoopTimer invalidate];
				
				[self displayError:@"Network unreachable"];
			}
			break;
		case INITIALIZING_STATE:
		{
			auto data = _connHandler->receiveData();
			if (data != nullptr)
			{
				auto eventRef = HQRemote::deserializeEvent(std::move(data));
				if (eventRef != nullptr && eventRef->event.type == HQRemote::HOST_INFO) {
					[_connLoopTimer invalidate];//stop handling the connection
					
					//create remote view
					RemoteViewController* nextView = [self.storyboard instantiateViewControllerWithIdentifier:@"remoteScreen"];
					
					ViewController* __weak weakSelf = self;
					
					dispatch_async(dispatch_get_main_queue(), ^{
						CGSize remoteFrameSize;
						remoteFrameSize.width = eventRef->event.hostInfo.width;
						remoteFrameSize.height = eventRef->event.hostInfo.height;
						
						nextView.remoteFrameSize = remoteFrameSize;
						
						nextView.connHandler = _connHandler;
						
						[weakSelf presentViewController:nextView animated:YES completion:nil];
						
						[weakSelf setInProgress:NO];
					});
				}
			}
		}
			break;
	}//switch (self.state)
}

@end
