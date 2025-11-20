use axum::{
    extract::{Json, State},
    http::{header, HeaderMap, HeaderValue, StatusCode},
    response::{Html, IntoResponse},
    routing::{get, post},
    Json as AxumJson, Router,
};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::{fs, net::TcpListener, sync::RwLock};

#[derive(Debug, Clone, Deserialize)]
struct SensorPayload {
    soil: i32,
    light: i32,
    is_watering: bool,
}

#[derive(Debug, Clone, Serialize)]
struct Snapshot {
    soil: i32,
    light: i32,
    is_watering: bool,
    auto_enabled: bool,
    updated_at: String,
}

#[derive(Debug, Clone, Default)]
struct CommandState {
    manual_water: bool,
    auto_enabled: bool,
}

#[derive(Clone)]
struct AppState {
    latest: Arc<RwLock<Option<Snapshot>>>,
    command: Arc<RwLock<CommandState>>,
}

#[derive(Debug, Serialize)]
struct CommandResponse {
    water_now: bool,
    auto_enabled: bool,
}

async fn index() -> Result<Html<String>, StatusCode> {
    match fs::read_to_string("./src/web/index.html").await {
        Ok(contents) => Ok(Html(contents)),
        Err(err) => {
            eprintln!("error index.html: {err}");
            Err(StatusCode::INTERNAL_SERVER_ERROR)
        }
    }
}

async fn styles() -> impl IntoResponse {
    match fs::read_to_string("./src/web/styles.css").await {
        Ok(css) => {
            let mut headers = HeaderMap::new();
            headers.insert(
                header::CONTENT_TYPE,
                HeaderValue::from_static("text/css; charset=utf-8"),
            );
            (headers, css).into_response()
        }
        Err(err) => {
            eprintln!("Error styles.css: {err}");
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Failed to load styles.css",
            )
                .into_response()
        }
    }
}

async fn scripts() -> impl IntoResponse {
    match fs::read_to_string("./src/web/scripts.js").await {
        Ok(js) => {
            let mut headers = HeaderMap::new();
            headers.insert(
                header::CONTENT_TYPE,
                HeaderValue::from_static("application/javascript; charset=utf-8"),
            );
            (headers, js).into_response()
        }
        Err(err) => {
            eprintln!("Error scripts.js: {err}");
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Failed to load scripts.js",
            )
                .into_response()
        }
    }
}

async fn receive_sensor(
    State(state): State<AppState>,
    Json(payload): Json<SensorPayload>,
) -> &'static str {
    println!("Payload: {:?}", payload);

    let auto_enabled = {
        let cmd = state.command.read().await;
        cmd.auto_enabled
    };

    let snap = Snapshot {
        soil: payload.soil,
        light: payload.light,
        is_watering: payload.is_watering,
        auto_enabled,
        updated_at: Utc::now().to_rfc3339(),
    };

    {
        let mut guard = state.latest.write().await;
        *guard = Some(snap);
    }

    "OK"
}

async fn latest(State(state): State<AppState>) -> AxumJson<Option<Snapshot>> {
    let data = state.latest.read().await.clone();
    AxumJson(data)
}

async fn trigger_water(State(state): State<AppState>) -> StatusCode {
    let mut cmd = state.command.write().await;
    cmd.manual_water = true;
    StatusCode::OK
}

async fn get_command(State(state): State<AppState>) -> AxumJson<CommandResponse> {
    let mut cmd = state.command.write().await;

    let resp = CommandResponse {
        water_now: cmd.manual_water,
        auto_enabled: cmd.auto_enabled,
    };

    cmd.manual_water = false;

    AxumJson(resp)
}

#[derive(Debug, Deserialize)]
struct AutoPayload {
    enabled: bool,
}

async fn set_auto(State(state): State<AppState>, Json(body): Json<AutoPayload>) -> StatusCode {
    let mut cmd = state.command.write().await;
    cmd.auto_enabled = body.enabled;
    println!("Set auto_enabled = {}", cmd.auto_enabled);
    StatusCode::OK
}

#[tokio::main]
async fn main() {
    let state = AppState {
        latest: Arc::new(RwLock::new(None)),
        command: Arc::new(RwLock::new(CommandState {
            manual_water: false,
            auto_enabled: true,
        })),
    };

    let app = Router::new()
        .route("/", get(index))
        .route("/styles.css", get(styles))
        .route("/scripts.js", get(scripts))
        .route("/sensor", post(receive_sensor))
        .route("/api/latest", get(latest))
        .route("/api/water", post(trigger_water))
        .route("/api/command", get(get_command))
        .route("/api/auto", post(set_auto))
        .with_state(state);

    let addr = "0.0.0.0:3000";
    println!("Server running on http://{addr}");

    let listener = TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}
