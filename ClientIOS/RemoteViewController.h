//
//  RemoteViewController.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "BaseViewControllerWithNetwork.h"

@interface RemoteViewController : BaseViewControllerWithNetwork

@property (strong, nonatomic) IBOutlet UIButton *exitBtn;
@property (strong, nonatomic) IBOutlet UIButton *flipYBtn;
@property (strong, nonatomic) IBOutlet UIButton *screenshotBtn;
@property (strong, nonatomic) IBOutlet UIButton *recordBtn;
@property (strong, nonatomic) IBOutlet UIImageView *remoteFrameView;

@property (nonatomic) CGSize remoteFrameSize;

@end
