//
//  ViewController.h
//  Client
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "BaseViewControllerWithNetwork.h"

@interface ViewController : BaseViewControllerWithNetwork

@property (strong, nonatomic) IBOutlet UITextField *ipText;
@property (strong, nonatomic) IBOutlet UITextField *portText;
@property (strong, nonatomic) IBOutlet UIButton *connectBtn;
@property (strong, nonatomic) IBOutlet UIActivityIndicatorView *progressBar;

@end

