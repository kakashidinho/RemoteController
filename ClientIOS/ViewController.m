//
//  ViewController.m
//  Client
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "ViewController.h"
#import "NetworkHandler.h"
#import "RemoteViewController.h"

#include <assert.h>

static NSString* gIPPreferenceKey = @"IP";
static NSString* gPortPreferenceKey = @"port";

typedef enum State {
	DISCONNECTED_STATE,
	INITIALIZING_STATE,
	INITIALIZED_STATE
} State;

typedef struct RemoteInitMsg {
	uint32_t width;
	uint32_t height;
} RemoteInitMsg;

/*-------- ViewController -----*/
@interface ViewController ()

@property (atomic) State state;

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
	NetworkHandler* networkHandler = [[NetworkHandler alloc] initWithIp:ip port:port delegate:self];
	
	self.networkHandler = networkHandler;
	
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

//stream delegate
- (id<NetworkHandlerDelegate>) onNetworkOpened {
	//TODO:
	[self setInProgress: NO];
	self.state = INITIALIZING_STATE;
	
	return self;
}

- (id<NetworkHandlerDelegate>) onNetworkClosedWithError: (NSString*) errorMsg {
	[self setInProgress: NO];
	
	[super onNetworkClosedWithError:errorMsg];
	
	return self;
}

- (id<NetworkHandlerDelegate>) onReceivedData: (NSData*) data {
	if (self.state == INITIALIZING_STATE) {
		//TODO: assume both sides use the same byte order for now
		RemoteInitMsg initMsg;
		assert(data.length == sizeof (initMsg));
		
		[data getBytes:&initMsg length:sizeof(initMsg)];
		
		//open remote view controller
		RemoteViewController* nextView = [self.storyboard instantiateViewControllerWithIdentifier:@"remoteScreen"];
		
		ViewController* __weak weakSelf = self;
		
		dispatch_async(dispatch_get_main_queue(), ^{
			CGSize remoteFrameSize;
			remoteFrameSize.width = initMsg.width;
			remoteFrameSize.height = initMsg.height;
			
			nextView.remoteFrameSize = remoteFrameSize;
			
			[nextView transferNetworkHandlerFrom:weakSelf];
			[weakSelf presentViewController:nextView animated:YES completion:nil];
		});
	
		self.state = INITIALIZED_STATE;
		
		return nextView;
	}
	
	return self;
}

@end
